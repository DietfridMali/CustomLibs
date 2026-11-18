#include <cstring>
#include <cmath>

#include "linerenderer.h"
#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "shader.h"
#include "texture.h"
#ifndef OPENGL
#include "compute_shader.h"
#include "gpuzone.h"
#endif

// =================================================================================================

bool LineRenderer::Create(int capacity)
{
	Destroy();
	if (capacity < 1)
		capacity = 1;
	if (not m_buffer.Create(capacity))
		return false;
	m_lines.Resize(capacity);
	m_lineCapacity = capacity;
	m_capacity = capacity;
	m_count = 0;
	m_isResident = false;
	m_isAvailable = true;
	return true;
}


void LineRenderer::Destroy(void)
{
	m_buffer.Destroy();
	m_faceBuffer.Destroy();
#ifndef OPENGL
	m_slotBuffer.Destroy();
	m_visibleBuffer.Destroy();
	m_commandBuffer.Destroy();
	m_slotCapacity = 0;
	m_visibleCapacity = 0;
	m_commandCapacity = 0;
#endif
	m_lines.Reset();
	m_faces.Reset();
	m_lineCapacity = 0;
	m_capacity = 0;
	m_faceCapacity = 0;
	m_count = 0;
	m_isResident = false;
	m_isAvailable = false;
	m_hasFaces = false;
}

// -------------------------------------------------------------------------------------------------
// Grow on demand, doubling. Reserve () is the batch being collected on the CPU, ReserveBuffer () the
// GPU buffer all batches of a frame are appended to. GfxArray::Create () starts a new buffer (the old
// one is released once the frames using it are through), so the append position starts over with it.

bool LineRenderer::Reserve(int count)
{
	if (count <= m_lineCapacity)
		return true;

	int newCapacity = m_lineCapacity ? m_lineCapacity : 1;

	while (newCapacity < count)
		newCapacity *= 2;
	m_lines.Resize(newCapacity);
	m_lineCapacity = newCapacity;
	return true;
}


bool LineRenderer::ReserveBuffer(int count)
{
	if (count <= m_capacity)
		return true;

	int newCapacity = m_capacity ? m_capacity : 1;

	while (newCapacity < count)
		newCapacity *= 2;
	if (not m_buffer.Create(newCapacity)) {
		m_isAvailable = false;
		return false;
	}
	m_capacity = newCapacity;
	return true;
}


bool LineRenderer::ReserveFaceBuffer(void)
{
	if (m_faceCapacity >= m_capacity)
		return true;
	if (not m_faceBuffer.Create(m_capacity)) {
		m_isAvailable = false;
		return false;
	}
	m_faceCapacity = m_capacity;
	return true;
}


#ifndef OPENGL
bool LineRenderer::ReserveSlotBuffer(void)
{
	if (m_slotCapacity >= m_capacity)
		return true;
	m_slotCapacity = 0;
	if (not m_slotBuffer.Create(m_capacity)) {
		m_isAvailable = false;
		return false;
	}

	uint32_t* slots = m_slotBuffer.m_data.DataPtr(0);

	for (int i = 0; i < m_capacity; ++i)
		slots[i] = uint32_t(i);
	if (not m_slotBuffer.UploadRange(0, m_capacity, false)) {
		m_isAvailable = false;
		return false;
	}
	m_slotCapacity = m_capacity;
	return true;
}


bool LineRenderer::ReserveVisibleBuffer(void)
{
	if (m_visibleCapacity >= m_capacity)
		return true;
	m_visibleCapacity = 0;
	if (not m_visibleBuffer.Create(m_capacity))
		return false;
	m_visibleCapacity = m_capacity;
	return true;
}


bool LineRenderer::ReserveCommands(int count)
{
	if (count <= m_commandCapacity)
		return true;

	int capacity = (m_commandCapacity > 0) ? m_commandCapacity : 1;

	while (capacity < count)
		capacity *= 2;
	m_commandCapacity = 0;
	if (not m_commandBuffer.Create(capacity))
		return false;
	m_commandCapacity = capacity;
	return true;
}
#endif

// -------------------------------------------------------------------------------------------------

static int CapFlags(LineRenderer::Cap cap, int flatFlag, int miterFlag)
{
	return (cap == LineRenderer::Cap::Flat) ? flatFlag : (cap == LineRenderer::Cap::Miter) ? miterFlag
																						   : 0;
}


bool LineRenderer::Add(const Vector3f& p0, const Vector3f& p1, float width, const RGBAColor& color, Style style, float phase,
					   Cap startCap, float startValue, Cap endCap, float endValue)
{
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
	line.pad[0] = float(CapFlags(startCap, 1, 4) + CapFlags(endCap, 2, 8));
	line.pad[1] = startValue;
	line.pad[2] = endValue;
	return true;
}


bool LineRenderer::AddFaced(const Vector3f& p0, const Vector3f& p1, float width, const RGBAColor& color, const Vector3f& n0,
							const Vector3f& n1, float contourWidth, float contourAlpha)
{
	if (not Add(p0, p1, width, color))
		return false;
	if (m_faces.Length() < m_lineCapacity)
		m_faces.Resize(m_lineCapacity);

	Faces& faces = m_faces[m_count - 1];

	faces.n0 = n0;
	faces.contourWidth = contourWidth;
	faces.n1 = n1;
	faces.contourAlpha = contourAlpha;
	m_hasFaces = true;
	return true;
}


float LineRenderer::MiterSlope(const Vector3f& p0, const Vector3f& p1, bool atEnd, const Vector3f& other)
{
	const float* m = baseRenderer.ModelView().AsArray();

	auto ToView = [m](const Vector3f& p) {
		return Vector3f(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
						m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
						m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
	};

	Vector3f	v0 = ToView(p0);
	Vector3f	v1 = ToView(p1);
	Vector3f	joint = atEnd ? v1 : v0;
	Vector3f	seg = v1 - v0;
	float		segLen = seg.Length();

	if (segLen < 1e-6f)
		return 0.0f;

	Vector3f	axis = seg * (1.0f / segLen);
	bool		perspective = (baseRenderer.Projection().AsArray()[11] != 0.0f);
	Vector3f	toEye = perspective ? (v0 + v1) * -0.5f : Vector3f(0.0f, 0.0f, 1.0f);

	toEye.Normalize();

	Vector3f	perp = axis.Cross(toEye);
	float		perpLen = perp.Length();

	if (perpLen < 1e-4f)
		return 0.0f;
	perp = perp * (1.0f / perpLen);

	Vector3f	toOther = ToView(other) - joint;
	float		otherLen = toOther.Length();

	if (otherLen < 1e-6f)
		return 0.0f;
	toOther = toOther * (1.0f / otherLen);

	float ox = toOther.Dot(axis) + (atEnd ? -1.0f : 1.0f);
	float oy = toOther.Dot(perp);

	if (std::fabs(oy) < 1e-4f)
		return 0.0f;
	return atEnd ? ox / oy : -ox / oy;
}


bool LineRenderer::AddStrip(const Vector3f* points, int count, float width, const RGBAColor& color, Style style, bool closed)
{
	if (not (points and (count > 1)))
		return false;

	int		segments = closed ? count : count - 1;
	float	phase = 0.0f;

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

void LineRenderer::SetupQuad(void)
{
	if (m_quadReady)
		return;
	m_quadReady = true;
	m_quad.Setup(
		{ Vector3f(-0.5f, -0.5f, 0.0f), Vector3f(0.5f, -0.5f, 0.0f), Vector3f(0.5f, 0.5f, 0.0f), Vector3f(-0.5f, 0.5f, 0.0f),
		  Vector3f(-0.5f, -0.5f, 0.0f), Vector3f(-0.5f, -0.5f, 0.0f), Vector3f(-0.5f, 0.5f, 0.0f), Vector3f(-0.5f, 0.5f, 0.0f),
		  Vector3f(0.5f, -0.5f, 0.0f), Vector3f(0.5f, -0.5f, 0.0f), Vector3f(0.5f, 0.5f, 0.0f), Vector3f(0.5f, 0.5f, 0.0f) },
		{ TexCoord{ 1, 0 }, TexCoord{ 2, 0 }, TexCoord{ 2, 1 }, TexCoord{ 1, 1 },
		  TexCoord{ 0, 0 }, TexCoord{ 1, 0 }, TexCoord{ 1, 1 }, TexCoord{ 0, 1 },
		  TexCoord{ 2, 0 }, TexCoord{ 3, 0 }, TexCoord{ 3, 1 }, TexCoord{ 2, 1 } });
}


bool LineRenderer::UploadBatch(int firstLine)
{
	std::memcpy(m_buffer.m_data.Data() + firstLine, m_lines.Data(), size_t(m_count) * sizeof(Line));
	if (not m_buffer.UploadRange(firstLine, m_count, false))
		return false;
	if (not m_hasFaces)
		return true;
	if (not ReserveFaceBuffer())
		return false;
	std::memcpy(m_faceBuffer.m_data.Data() + firstLine, m_faces.Data(), size_t(m_count) * sizeof(Faces));
	return m_faceBuffer.UploadRange(firstLine, m_count, false);
}


bool LineRenderer::Upload(void)
{
	m_isResident = false;
	if (not m_isAvailable or (m_count <= 0))
		return false;
	if (not ReserveBuffer(m_count))
		return false;
	if (not UploadBatch(0))
		return false;
	m_isResident = true;
	return true;
}


bool LineRenderer::Render(void)
{
	if (not m_isAvailable or (m_count <= 0))
		return false;

	int firstLine = 0;

	if (not m_isResident) {
		if (not ReserveBuffer(m_buffer.AppendBase() + m_count))
			return false;
		firstLine = m_buffer.AppendBase();
		if (not UploadBatch(firstLine))
			return false;
		m_buffer.SetAppendBase(firstLine + m_count);
	}

	DrawStates	states;
	Shader*		shader = BeginDraw(firstLine, states);
	bool		ok = false;

	if (shader != nullptr) {
		m_quad.GetGfxDataLayout().SetInstanceCount(uint32_t(m_count));
		ok = m_quad.Render(shader);
		m_quad.GetGfxDataLayout().SetInstanceCount(1);
	}
	EndDraw(states, shader != nullptr);
	return ok;
}


#ifndef OPENGL
bool LineRenderer::RenderRanges(const Range* ranges, int count)
{
	if (not (m_isAvailable and m_isResident and m_hasFaces) or (count <= 0))
		return false;
	if (not ReserveCommands(m_commandBuffer.AppendBase() + count))
		return false;
	SetupQuad();
	if (not m_quad.UpdateData())
		return false;

	int				firstCommand = m_commandBuffer.AppendBase();
	GfxDrawCommand*	commands = m_commandBuffer.m_data.DataPtr(firstCommand);
	uint32_t		indexCount = m_quad.Indices().GfxDataLength();
	int				commandCount = 0;

	for (int i = 0; i < count; ++i) {
		if (ranges[i].count <= 0)
			continue;

		GfxDrawCommand& command = commands[commandCount++];

		command.indexCount = indexCount;
		command.instanceCount = uint32_t(ranges[i].count);
		command.firstIndex = 0;
		command.baseVertex = 0;
		command.firstInstance = uint32_t(ranges[i].first);
	}
	if ((commandCount == 0) or (indexCount == 0) or not m_commandBuffer.UploadRange(firstCommand, commandCount, false))
		return false;
	m_commandBuffer.SetAppendBase(firstCommand + commandCount);

	DrawStates	states;
	Shader*		shader = BeginDraw(0, states);

	if (shader != nullptr)
		m_quad.GetGfxDataLayout().RenderIndirect({}, m_commandBuffer, uint32_t(firstCommand), uint32_t(commandCount));
	EndDraw(states, shader != nullptr);
	return shader != nullptr;
}


bool LineRenderer::RenderCulled(int count, RenderTarget* const* depthLevels, int depthLevelCount)
{
	static constexpr int cCullGroupSize = 256;

	if (count > m_count)
		count = m_count;
	if (not (m_isAvailable and m_isResident and m_hasFaces) or (count <= 0))
		return false;
	if ((depthLevels == nullptr) or (depthLevelCount <= 0))
		depthLevelCount = 0;
	if (depthLevelCount > cMaxDepthLevels)
		depthLevelCount = cMaxDepthLevels;
	ComputeShader* cull = baseShaderHandler.SetupComputeShader((depthLevelCount > 0) ? "lineCullDepth" : "lineCull");
	if ((cull == nullptr) or not cull->IsValid())
		return false;
	if (not (ReserveVisibleBuffer() and ReserveCommands(m_commandBuffer.AppendBase() + 1)))
		return false;
	SetupQuad();
	if (not m_quad.UpdateData())
		return false;

	int				firstCommand = m_commandBuffer.AppendBase();
	GfxDrawCommand&	command = m_commandBuffer.m_data[firstCommand];

	command.indexCount = m_quad.Indices().GfxDataLength();
	command.instanceCount = 0;
	command.firstIndex = 0;
	command.baseVertex = 0;
	command.firstInstance = 0;
	if (not m_commandBuffer.UploadRange(firstCommand, 1, true))
		return false;
	m_commandBuffer.SetAppendBase(firstCommand + 1);

	Vector2f texelSize = TargetTexelSize();

	cull->SetVector2f("texelSize", texelSize);
	cull->SetInt("lineCount", count);
	cull->SetInt("firstCommand", firstCommand);
	cull->SetFloat("minLength", m_minLength);
	cull->SetFloat("haloReach", m_haloReach);
	if (depthLevelCount > 0) {
		cull->SetFloat("depthPull", m_depthPull);
		cull->SetInt("depthLevelCount", depthLevelCount);
		cull->SetInt("screenWidth", int(std::lround(1.0f / texelSize.X())));
		cull->SetInt("screenHeight", int(std::lround(1.0f / texelSize.Y())));
		for (int i = 0; i < cMaxDepthLevels; ++i)
			cull->BindSampledImage(uint32_t(4 + i), depthLevels[(i < depthLevelCount) ? i : depthLevelCount - 1], 0);
	}
	m_buffer.BindReadOnly(0);
	m_faceBuffer.BindReadOnly(1);
	m_visibleBuffer.Bind(0);
	m_commandBuffer.Bind(1);
	m_visibleBuffer.ComputeAccessBarrier();

	bool isCulled = false;
	{
		GfxGpuZone("line culling");
		isCulled = cull->Dispatch(uint32_t((count + cCullGroupSize - 1) / cCullGroupSize), 1, 1);
	}
	m_commandBuffer.Release(1);
	m_visibleBuffer.Release(0);
	m_faceBuffer.ReleaseReadOnly(1);
	m_buffer.ReleaseReadOnly(0);
	m_visibleBuffer.ComputeAccessBarrier();
	m_commandBuffer.ComputeAccessBarrier();
	if (not isCulled)
		return false;

	DrawStates	states;
	Shader*		shader = BeginDraw(0, states, true);

	if (shader != nullptr)
		m_quad.GetGfxDataLayout().RenderIndirect({}, m_commandBuffer, uint32_t(firstCommand), 1);
	EndDraw(states, shader != nullptr);
	return shader != nullptr;
}
#endif


Vector2f LineRenderer::TargetTexelSize(void)
{
	RenderTarget* renderTarget = baseRenderer.GetActiveBuffer();
	if (renderTarget != nullptr)
		return renderTarget->TexelSize();
	return Vector2f(1.0f / float(baseRenderer.WindowWidth()), 1.0f / float(baseRenderer.WindowHeight()));
}


Shader* LineRenderer::BeginDraw(int firstLine, DrawStates& states, bool isCulled)
{
	// states feed the PSO, so they are set before the shader is activated. Alpha blending for the
	// antialiased edge; a ribbon is never culled. Depth test and write stay what the caller set.
	states.blending = gfxStates.SetBlending(1);
	states.operation = gfxStates.GetBlendEquation();
	gfxStates.GetBlendFunc(states.source, states.destination);
	baseRenderer.SetBlendMode(GfxOperations::BlendMode::Alpha, m_darken ? GfxOperations::BlendOp::Min : GfxOperations::BlendOp::Add);
	states.culling = gfxStates.SetFaceCulling(0);
#ifndef OPENGL
	if (m_hasFaces and not isCulled and not ReserveSlotBuffer())
		return nullptr;
#endif

	Shader* shader = baseShaderHandler.SetupRenderShader(m_hasFaces ? "lineDrawFaces" : (m_occluderDepth != nullptr) ? "lineDrawHidden" : "lineDraw");

	if (shader == nullptr)
		return nullptr;
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
	shader->SetVector2f("texelSize", TargetTexelSize());
	shader->SetFloat("dashScale", m_dashScale);
	shader->SetFloat("antialias", m_antialias ? 1.0f : 0.0f);
	shader->SetInt("firstLine", firstLine);
	shader->SetFloat("viewerPull", m_viewerPull);
	shader->SetFloat("worldWidth", m_worldWidth);
	shader->SetFloat("darken", m_darken ? 1.0f : 0.0f);
	m_buffer.BindReadOnly(0);
	if (UsesOccluderDepth()) {
		m_occluderDepth->Activate(2);
		if (m_occluderMask != nullptr) {
			m_occluderMask->Activate(3);
			shader->SetFloat("occluderMaskLimit", m_occluderMaskLimit);
		}
		else {
			m_occluderDepth->Activate(3);
			shader->SetFloat("occluderMaskLimit", 1e30f);
		}
	}
	if (m_hasFaces) {
		shader->SetFloat("widthFadeDistance", m_widthFadeDistance);
		shader->SetFloat("alphaFadeScale", m_alphaFadeScale);
		shader->SetFloat("depthPull", m_depthPull);
		shader->SetFloat("widthRefDistance", m_widthRefDistance);
		shader->SetFloat("minWidthScale", m_minWidthScale);
		shader->SetFloat("haloReach", m_haloReach);
		shader->SetFloat("haloStrength", m_haloStrength);
		m_faceBuffer.BindReadOnly(1);
#ifndef OPENGL
		if (isCulled)
			m_visibleBuffer.BindInstanceStream("Uint", 0);
		else
			m_slotBuffer.BindInstanceStream("Uint", 0);
#endif
	}
	return shader;
}


void LineRenderer::EndDraw(const DrawStates& states, bool isBound)
{
	if (isBound) {
		if (m_hasFaces) {
#ifndef OPENGL
			m_slotBuffer.ReleaseInstanceStream("Uint", 0);
#endif
			m_faceBuffer.ReleaseReadOnly(1);
		}
		if (UsesOccluderDepth()) {
			if (m_occluderMask != nullptr)
				m_occluderMask->Deactivate();
			m_occluderDepth->Deactivate();
		}
		m_buffer.ReleaseReadOnly(0);
	}
	gfxStates.SetFaceCulling(states.culling);
	gfxStates.BlendEquation(states.operation);
	gfxStates.BlendFunc(states.source, states.destination);
	gfxStates.SetBlending(states.blending);
}

// =================================================================================================
