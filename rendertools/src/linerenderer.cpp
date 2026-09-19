#include <cstring>
#include <cstdio>

#include "linerenderer.h"
#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "shader.h"

// =================================================================================================

#if DBG_DIRECTX
static const void* watchedResource = nullptr;

static void CheckWatchedResource(const void* resource, const char* where, int line) noexcept {
    if (resource != watchedResource) {
        fprintf(stderr, "line buffer resource pointer changed from %p to %p, noticed in %s at %s:%d\n", watchedResource, resource, where, __FILE__, line);
        watchedResource = resource;
    }
}
#endif


bool LineRenderer::Create(int capacity) {
#if DBG_DIRECTX
    fprintf(stderr, "line renderer Create (%d) at %s:%d (LineRenderer @%p)\n", capacity, __FILE__, __LINE__, static_cast<void*>(this));
#endif
    Destroy();
    if (capacity < 1)
        capacity = 1;
    if (not m_buffer.Create(capacity))
        return false;
#if DBG_DIRECTX
    watchedResource = m_buffer.m_resource.Get();
#endif
    m_lines.Resize(capacity);
    m_lineCapacity = capacity;
    m_capacity = capacity;
    m_count = 0;
    m_isAvailable = true;
    return true;
}


void LineRenderer::Destroy(void) {
#if DBG_DIRECTX
    fprintf(stderr, "line renderer Destroy at %s:%d (LineRenderer @%p)\n", __FILE__, __LINE__, static_cast<void*>(this));
#endif
    m_buffer.Destroy();
    m_lines.Reset();
    m_lineCapacity = 0;
    m_capacity = 0;
    m_count = 0;
    m_isAvailable = false;
}

// -------------------------------------------------------------------------------------------------
// Grow on demand, doubling. Reserve () is the batch being collected on the CPU, ReserveBuffer () the
// GPU buffer all batches of a frame are appended to. GfxArray::Create () starts a new buffer (the old
// one is released once the frames using it are through), so the append position starts over with it.

bool LineRenderer::Reserve(int count) {
    if (count <= m_lineCapacity)
        return true;

    int newCapacity = m_lineCapacity ? m_lineCapacity : 1;

    while (newCapacity < count)
        newCapacity *= 2;
    m_lines.Resize(newCapacity);
    m_lineCapacity = newCapacity;
    return true;
}


bool LineRenderer::ReserveBuffer(int count) {
    if (count <= m_capacity)
        return true;

    int newCapacity = m_capacity ? m_capacity : 1;

    while (newCapacity < count)
        newCapacity *= 2;
#if DBG_DIRECTX
    fprintf(stderr, "line renderer grows buffer %d -> %d at %s:%d (LineRenderer @%p)\n", m_capacity, newCapacity, __FILE__, __LINE__, static_cast<void*>(this));
#endif
    if (not m_buffer.Create(newCapacity)) {
        m_isAvailable = false;
        return false;
    }
#if DBG_DIRECTX
    watchedResource = m_buffer.m_resource.Get();
#endif
    m_capacity = newCapacity;
    return true;
}

// -------------------------------------------------------------------------------------------------

bool LineRenderer::Add(const Vector3f& p0, const Vector3f& p1, float width, const RGBAColor& color, Style style, float phase) {
    if (not m_isAvailable)
        return false;
    if (not Reserve(m_count + 1))
        return false;

    Line& line = m_lines[m_count++];

    line.p0 = p0;
    line.width = width;
    line.p1 = p1;
    line.style = float(int(style));
    line.color = color;
    line.phase = phase;
    line.pad[0] = line.pad[1] = line.pad[2] = 0.0f;
    return true;
}


bool LineRenderer::AddStrip(const Vector3f* points, int count, float width, const RGBAColor& color, Style style, bool closed) {
    if (not (points and (count > 1)))
        return false;

    int   segments = closed ? count : count - 1;
    float phase = 0.0f;

    if (not Reserve(m_count + segments))
        return false;
    for (int i = 0; i < segments; i++) {
        const Vector3f& p0 = points[i];
        const Vector3f& p1 = points[(i + 1) % count];

        if (not Add(p0, p1, width, color, style, phase))
            return false;
        phase += (p1 - p0).Length();
    }
    return true;
}

// -------------------------------------------------------------------------------------------------

void LineRenderer::SetupQuad(void) {
    if (m_quadReady)
        return;
    m_quadReady = true;
    m_quad.Setup(
        { Vector3f(-0.5f, -0.5f, 0.0f), Vector3f(0.5f, -0.5f, 0.0f), Vector3f(0.5f, 0.5f, 0.0f), Vector3f(-0.5f, 0.5f, 0.0f) },
        { TexCoord{ 0, 0 }, TexCoord{ 1, 0 }, TexCoord{ 1, 1 }, TexCoord{ 0, 1 } }
    );
}


bool LineRenderer::Render(void) {
    if (not m_isAvailable or (m_count <= 0))
        return false;
#if DBG_DIRECTX
    CheckWatchedResource(m_buffer.m_resource.Get(), "Render (start)", __LINE__);
#endif
    if (not ReserveBuffer(m_buffer.AppendBase() + m_count))
        return false;

    int firstLine = m_buffer.AppendBase();

    std::memcpy(m_buffer.m_data.Data() + firstLine, m_lines.Data(), size_t(m_count) * sizeof(Line));
    if (not m_buffer.UploadRange(firstLine, m_count, false))
        return false;
    m_buffer.SetAppendBase(firstLine + m_count);

    // states feed the PSO, so they are set before the shader is activated. Alpha blending for the
    // antialiased edge; a ribbon is never culled. Depth test and write stay what the caller set.
    int prevBlend = gfxStates.SetBlending(1);
    GfxOperations::BlendFactor prevSrc;
    GfxOperations::BlendFactor prevDst;

    gfxStates.GetBlendFunc(prevSrc, prevDst);
    baseRenderer.SetBlendMode(GfxOperations::BlendMode::Alpha);

    int prevCull = gfxStates.SetFaceCulling(0);

    Shader* shader = baseShaderHandler.SetupRenderShader("lineDraw");
    bool ok = false;

    if (shader != nullptr) {
        SetupQuad();
        // Perspective or orthographic: a perspective matrix has the w row's z coefficient (-1 or 1) where
        // an orthographic one has 0. Column major, so that is element 11. The shader divides the pixel
        // scale by the depth only for a perspective projection.
        const float* projection = baseRenderer.Projection().AsArray();

        shader->SetFloat("perspective", (projection[11] != 0.0f) ? 1.0f : 0.0f);
        // What the VS converts pixels into view units with: the texel size of the ACTIVE RENDER TARGET.
        // The VS measures the projection through mViewport, i.e. in the NDC of the whole target buffer
        // (the gfx viewport always spans the buffer; mViewport scales into the sub rectangle), so the
        // pixel scale is the buffer's - baseRenderer.TexelSize () describes the viewport and inflated a
        // line drawn into a 370 px wide menu canvas on a 1920 px screen five times. Without an active
        // buffer the window is the target.
        RenderTarget* renderTarget = baseRenderer.GetActiveBuffer();
        TexCoord texelSize = renderTarget
            ? renderTarget->TexelSize()
            : TexCoord(1.0f / float(baseRenderer.WindowWidth()), 1.0f / float(baseRenderer.WindowHeight()));

        shader->SetVector2f("texelSize", texelSize);
        shader->SetFloat("dashScale", m_dashScale);
        shader->SetFloat("antialias", m_antialias ? 1.0f : 0.0f);
        shader->SetInt("firstLine", firstLine);
        m_buffer.Bind(0);
        m_quad.GetGfxDataLayout().SetInstanceCount(uint32_t(m_count));
        ok = m_quad.Render(shader);
        m_quad.GetGfxDataLayout().SetInstanceCount(1);
        m_buffer.Release(0);
    }

    gfxStates.SetFaceCulling(prevCull);
    gfxStates.BlendFunc(prevSrc, prevDst);
    gfxStates.SetBlending(prevBlend);
#if DBG_DIRECTX
    CheckWatchedResource(m_buffer.m_resource.Get(), "Render (end)", __LINE__);
#endif
    return ok;
}

// =================================================================================================
