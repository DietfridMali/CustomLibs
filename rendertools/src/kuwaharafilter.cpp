#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "gfxrenderer.h"
#include "gfxstates.h"
#include "base_shaderhandler.h"
#include "texturebuffer.h"
#include "kuwaharafilter.h"
#include "loghandler.h"

// =================================================================================================

static int TensorMargin(const KuwaharaFilter::Params& params) {
    return int(std::ceil(3.0f * params.tensorSigma));
}


KuwaharaFilter::Targets* KuwaharaFilter::GetTargets(int width, int height, int margin, GfxPixelFormat filterFormat) {
    for (Targets& targets : m_targets) {
        if ((targets.width == width) and (targets.height == height) and (targets.margin == margin) and (targets.filterFormat == filterFormat))
            return &targets;
    }

    Targets targets;
    targets.width = width;
    targets.height = height;
    targets.margin = margin;
    targets.filterFormat = filterFormat;
    targets.filter = new (std::nothrow) RenderTarget();
    targets.tensor = new (std::nothrow) RenderTarget();

    RenderTarget::RTCreationParams filterParams;
    filterParams.name = "kuwaharaFilter";
    filterParams.colorBufferCount = 1;
    filterParams.depthBufferCount = 0;
    filterParams.colorFormat = ToNativeColorFormat(filterFormat);
    filterParams.hasMRTs = false;

    RenderTarget::RTCreationParams tensorParams;
    tensorParams.name = "kuwaharaTensor";
    tensorParams.colorBufferCount = 2;
    tensorParams.depthBufferCount = 0;
    tensorParams.colorFormat = ToNativeColorFormat(GfxPixelFormat::RGBA16_SFloat);
    tensorParams.hasMRTs = true;

    if (not (targets.filter and targets.tensor and
             targets.filter->Create(width, height, 1, filterParams) and
             targets.tensor->Create(width + 2 * margin, height + 2 * margin, 1, tensorParams))) {
        delete targets.filter;
        delete targets.tensor;
        return nullptr;
    }
    m_targets.Append(targets);
    return &m_targets[m_targets.Length() - 1];
}


void KuwaharaFilter::Destroy(void) {
    for (Targets& targets : m_targets) {
        delete targets.filter;
        delete targets.tensor;
    }
    m_targets.Clear();
}


Shader* KuwaharaFilter::SetupShader(const char* shaderId, int width, int height, bool wrapU, bool wrapV, const Params& params) {
    Shader* shader = baseShaderHandler.SetupRenderShader(shaderId);
    if (shader) {
        shader->SetInt("width", width);
        shader->SetInt("height", height);
        shader->SetInt("wrapU", wrapU ? 1 : 0);
        shader->SetInt("wrapV", wrapV ? 1 : 0);
        shader->SetFloat("alphaThreshold", params.alphaThreshold);
    }
    return shader;
}


void KuwaharaFilter::SetupCubeFace(Shader* shader, const Targets& targets, int face) {
    shader->SetInt("face", face);
    shader->SetInt("faceSize", targets.width);
    shader->SetInt("margin", targets.margin);
}


void KuwaharaFilter::SetPassStates(void) {
    gfxStates.SetDepthTest(0);
    gfxStates.SetDepthWrite(0);
    gfxStates.SetBlending(0);
    gfxStates.SetFaceCulling(0);
    gfxStates.SetScissorTest(0);
}


void KuwaharaFilter::SetPassArea(const Viewport* area, int margin, int width, int height) {
    if (not area)
        return;
    int left = std::max(area->Left() - margin, 0);
    int top = std::max(area->Top() - margin, 0);
    int right = std::min(area->Left() + area->Width() + margin, width);
    int bottom = std::min(area->Top() + area->Height() + margin, height);
    int areaWidth = std::max(right - left, 0);
    int areaHeight = std::max(bottom - top, 0);
    gfxStates.SetScissor(left, baseRenderer.UsesOpenGL() ? height - top - areaHeight : top, areaWidth, areaHeight);
    gfxStates.SetScissorTest(1);
}


int KuwaharaFilter::SourceMargin(const Params& params) {
    return std::max(2 * params.radius, TensorMargin(params) + 1);
}


bool KuwaharaFilter::RenderTensor(Targets& targets, Texture* source, const Params& params, int face, const Viewport* area) {
    bool isCube = (face >= 0);
    bool wrapU = params.wrapU and not isCube;
    bool wrapV = params.wrapV and not isCube;
    int tensorWidth = targets.width + 2 * targets.margin;
    int tensorHeight = targets.height + 2 * targets.margin;
    int areaMargin = TensorMargin(params);
    if (not targets.tensor->Activate({ .bufferIndex = 0, .drawBufferGroup = RenderTarget::dbSingle, .clear = (area == nullptr) }))
        return false;
    SetPassArea(area, areaMargin, tensorWidth, tensorHeight);
    Shader* shader = SetupShader(isCube ? "kuwaharaCubeTensor" : "kuwaharaTensor", tensorWidth, tensorHeight, wrapU, wrapV, params);
    bool ok = (shader != nullptr);
    if (ok) {
        if (baseRenderer.UsesOpenGL())
            shader->SetInt("srcTex", 0);
        if (isCube)
            SetupCubeFace(shader, targets, face);
        ok = targets.tensor->RenderAsTexture(source, { .destination = -1, .shader = shader });
    }
    for (int pass = 0; ok and (pass < 2); ++pass) {
        shader = SetupShader("kuwaharaTensorBlur", tensorWidth, tensorHeight, wrapU, wrapV, params);
        ok = (shader != nullptr);
        if (ok) {
            if (baseRenderer.UsesOpenGL())
                shader->SetInt("tensorTex", 0);
            shader->SetInt("directionX", (pass == 0) ? 1 : 0);
            shader->SetInt("directionY", (pass == 0) ? 0 : 1);
            shader->SetFloat("sigma", params.tensorSigma);
            if (area) {
                Texture* tensor = targets.tensor->GetAsTexture({ .source = pass });
                ok = (tensor != nullptr) and targets.tensor->Activate({ .bufferIndex = 1 - pass, .drawBufferGroup = RenderTarget::dbSingle, .clear = false, .reactivate = true });
                if (ok) {
                    SetPassArea(area, areaMargin, tensorWidth, tensorHeight);
                    ok = targets.tensor->RenderAsTexture(tensor, { .destination = -1, .clearBuffer = false, .shader = shader });
                }
            }
            else
                ok = targets.tensor->Render({ .source = pass, .destination = 1 - pass, .clearBuffer = true, .shader = shader });
        }
    }
    targets.tensor->Deactivate();
    return ok;
}


bool KuwaharaFilter::RenderFilter(Targets& targets, Texture* source, const Params& params, int face, const Viewport* area) {
    bool isCube = (face >= 0);
    if (not targets.filter->Activate({ .bufferIndex = 0, .drawBufferGroup = RenderTarget::dbSingle, .clear = (area == nullptr) }))
        return false;
    SetPassArea(area, 0, targets.width, targets.height);
    Shader* shader = SetupShader(isCube ? "kuwaharaCubeFilter" : "kuwaharaFilter", targets.width, targets.height,
                                 params.wrapU and not isCube, params.wrapV and not isCube, params);
    bool ok = (shader != nullptr) and targets.tensor->BindBuffer(0, 1);
    if (ok) {
        if (baseRenderer.UsesOpenGL()) {
            shader->SetInt("srcTex", 0);
            shader->SetInt("tensorTex", 1);
        }
        if (isCube)
            SetupCubeFace(shader, targets, face);
        shader->SetFloat("radius", float(params.radius));
        shader->SetFloat("anisotropy", params.anisotropy);
        shader->SetFloat("sharpness", params.sharpness);
        shader->SetFloat("minSigma", params.minSigma);
        ok = targets.filter->RenderAsTexture(source, { .destination = -1, .shader = shader });
    }
    targets.filter->Deactivate();
    return ok;
}


bool KuwaharaFilter::ReplaceTexture(Texture* texture, AutoArray<uint8_t>& pixels, int width, int height, int faceCount) {
    eColorEncoding colorEncoding = texture->ColorEncoding(0);
    size_t faceBytes = size_t(width) * size_t(height) * 4u;
    AutoArray<TextureBuffer*> buffers;
    bool ok = true;
    for (int face = 0; ok and (face < faceCount); ++face) {
        TextureBuffer* buffer = new (std::nothrow) TextureBuffer();
        ok = (buffer != nullptr) and buffer->Allocate(width, height, 4, pixels.Data() + faceBytes * size_t(face));
        if (ok) {
            buffer->m_info.m_colorEncoding = colorEncoding;
            buffer->m_info.m_hasColorEncoding = true;
            buffers.Append(buffer);
        }
        else
            delete buffer;
    }
    if (ok)
        ok = texture->Create();
    if (not ok) {
        for (TextureBuffer* buffer : buffers)
            delete buffer;
        return false;
    }
    for (TextureBuffer* buffer : buffers)
        texture->m_buffers.Append(buffer);
    texture->m_compression = tcNone;
    return texture->Deploy();
}


bool KuwaharaFilter::RenderBlur(Targets& targets, Texture* source, const Params& params, int face) {
    bool isCube = (face >= 0);
    bool wrapU = params.wrapU and not isCube;
    bool wrapV = params.wrapV and not isCube;
    int blurWidth = targets.width + 2 * targets.margin;
    int blurHeight = targets.height + 2 * targets.margin;
    float sigma = float(params.blurRadius) / 1.6f;
    if (not targets.tensor->Activate({ .bufferIndex = 0, .drawBufferGroup = RenderTarget::dbSingle, .clear = true }))
        return false;
    Shader* shader = SetupShader(isCube ? "kuwaharaCubeSource" : "kuwaharaSource", blurWidth, blurHeight, wrapU, wrapV, params);
    bool ok = (shader != nullptr);
    if (ok) {
        if (baseRenderer.UsesOpenGL())
            shader->SetInt("srcTex", 0);
        if (isCube)
            SetupCubeFace(shader, targets, face);
        ok = targets.tensor->RenderAsTexture(source, { .destination = -1, .shader = shader });
    }
    if (ok) {
        shader = SetupShader("kuwaharaGauss", blurWidth, blurHeight, wrapU, wrapV, params);
        ok = (shader != nullptr);
        if (ok) {
            if (baseRenderer.UsesOpenGL())
                shader->SetInt("tensorTex", 0);
            shader->SetInt("directionX", 1);
            shader->SetInt("directionY", 0);
            shader->SetInt("blurRadius", params.blurRadius);
            shader->SetInt("offset", 0);
            shader->SetFloat("sigma", sigma);
            ok = targets.tensor->Render({ .source = 0, .destination = 1, .clearBuffer = true, .shader = shader });
        }
    }
    targets.tensor->Deactivate();
    if (not ok)
        return false;

    if (not targets.filter->Activate({ .bufferIndex = 0, .drawBufferGroup = RenderTarget::dbSingle, .clear = true }))
        return false;
    Texture* blurred = targets.tensor->GetAsTexture({ .source = 1 });
    shader = SetupShader("kuwaharaGauss", blurWidth, blurHeight, wrapU, wrapV, params);
    ok = (shader != nullptr) and (blurred != nullptr);
    if (ok) {
        if (baseRenderer.UsesOpenGL())
            shader->SetInt("tensorTex", 0);
        shader->SetInt("directionX", 0);
        shader->SetInt("directionY", 1);
        shader->SetInt("blurRadius", params.blurRadius);
        shader->SetInt("offset", targets.margin);
        shader->SetFloat("sigma", sigma);
        ok = targets.filter->RenderAsTexture(blurred, { .destination = -1, .shader = shader });
    }
    targets.filter->Deactivate();
    return ok;
}


bool KuwaharaFilter::RenderFaces(Targets& targets, Texture* texture, const Params& params, bool isCube, bool isBlur, AutoArray<uint8_t>& pixels) {
    int faceCount = isCube ? 6 : 1;
    size_t faceBytes = size_t(targets.width) * size_t(targets.height) * 4u;
    bool ok = true;
    for (int face = 0; ok and (face < faceCount); ++face) {
        int cubeFace = isCube ? face : -1;
        baseRenderer.PushViewport();
        baseRenderer.PushMatrix();
        baseRenderer.PushMatrix(RenderMatrices::mtProjection);
        baseRenderer.ResetTransformation();

        if (isBlur)
            ok = RenderBlur(targets, texture, params, cubeFace);
        else
            ok = RenderTensor(targets, texture, params, cubeFace) and RenderFilter(targets, texture, params, cubeFace);

        baseRenderer.PopMatrix(RenderMatrices::mtProjection);
        baseRenderer.PopMatrix();
        baseRenderer.PopViewport();

        if (ok)
            ok = targets.filter->ReadBuffer(0, pixels.Data() + faceBytes * size_t(face), faceBytes);
    }
    return ok;
}


bool KuwaharaFilter::Filter(Texture* texture, const Params& params, bool isCube) {
    int width = texture->GetWidth();
    int height = texture->GetHeight();
    if ((width <= 0) or (height <= 0))
        return false;
    int margin = isCube ? std::max(int(std::ceil(3.0f * params.tensorSigma)), params.blurRadius) : 0;
    int faceCount = isCube ? 6 : 1;
    Targets* targets = GetTargets(width, height, margin, GfxPixelFormat::RGBA8_UNorm);
    if (not targets)
        return false;

    Shader* prevShader = baseShaderHandler.m_activeShader;
    String prevShaderId = baseShaderHandler.m_activeShaderId;
    int depthTest = gfxStates.SetDepthTest(0);
    int depthWrite = gfxStates.SetDepthWrite(0);
    int blending = gfxStates.SetBlending(0);
    int faceCulling = gfxStates.SetFaceCulling(0);
    int scissorTest = gfxStates.SetScissorTest(0);

    size_t faceBytes = size_t(width) * size_t(height) * 4u;
    AutoArray<uint8_t> pixels;
    bool ok = (pixels.Resize(int32_t(faceBytes * size_t(faceCount))) != nullptr);
    if (ok)
        ok = RenderFaces(*targets, texture, params, isCube, false, pixels);
    if (ok and (params.blurRadius > 0))
        ok = ReplaceTexture(texture, pixels, width, height, faceCount) and RenderFaces(*targets, texture, params, isCube, true, pixels);

    gfxStates.SetScissorTest(scissorTest);
    gfxStates.SetFaceCulling(faceCulling);
    gfxStates.SetBlending(blending);
    gfxStates.SetDepthWrite(depthWrite);
    gfxStates.SetDepthTest(depthTest);
    if (prevShader)
        baseShaderHandler.SetupRenderShader(prevShaderId);
    else
        baseShaderHandler.StopShader();

    if (not (ok and ReplaceTexture(texture, pixels, width, height, faceCount))) {
        logHandler.Print("KuwaharaFilter: filtering '%s' failed (%d x %d x %d)\n", static_cast<const char*>(texture->GetName()), width, height, faceCount);
        return false;
    }
    return true;
}


bool KuwaharaFilter::Apply(Texture* texture, const Params& params) {
    return Filter(texture, params, false);
}


bool KuwaharaFilter::ApplyCube(Texture* cubemap, const Params& params) {
    return Filter(cubemap, params, true);
}


RenderTarget* KuwaharaFilter::FilterToTarget(Texture* source, int width, int height, GfxPixelFormat filterFormat, const Params& params, const Viewport* area) {
    Targets* targets = GetTargets(width, height, 0, filterFormat);
    if (not targets)
        return nullptr;

    SetPassStates();
    baseRenderer.PushViewport();
    baseRenderer.PushMatrix();
    baseRenderer.PushMatrix(RenderMatrices::mtProjection);
    baseRenderer.ResetTransformation();

    bool ok = RenderTensor(*targets, source, params, -1, area) and RenderFilter(*targets, source, params, -1, area);

    baseRenderer.PopMatrix(RenderMatrices::mtProjection);
    baseRenderer.PopMatrix();
    baseRenderer.PopViewport();
    gfxStates.SetScissorTest(0);
    gfxStates.SetDepthWrite(1);
    gfxStates.SetDepthTest(1);
    gfxStates.SetFaceCulling(1);
    gfxStates.BlendFunc(GfxOperations::BlendFactor::SrcAlpha, GfxOperations::BlendFactor::InvSrcAlpha);
    gfxStates.BlendEquation(GfxOperations::BlendOp::Add);
    gfxStates.SetBlending(0);
    return ok ? targets->filter : nullptr;
}


bool KuwaharaFilter::Prepare(int width, int height, GfxPixelFormat filterFormat) {
    return GetTargets(width, height, 0, filterFormat) != nullptr;
}


bool KuwaharaFilter::FilterToBuffer(Texture* source, int width, int height, float* dest, const Params& params) {
    RenderTarget* target = FilterToTarget(source, width, height, GfxPixelFormat::R32_SFloat, params);
    return (target != nullptr) and target->ReadBuffer(0, dest, size_t(width) * size_t(height) * sizeof(float));
}

// =================================================================================================
