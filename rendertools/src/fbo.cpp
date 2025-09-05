#include "glew.h"
#include "conversions.hpp"
#include "fbo.h"
#include "tristate.h"
#include "base_renderer.h"
#include "base_shaderhandler.h"

GLuint FBO::m_activeHandle = GL_NONE;

// =================================================================================================

FBO::FBO() {
    Init();
}


void FBO::Init(void) {
    m_handle = 0;
    m_width = 0;
    m_height = 0;
    m_scale = 1;
    m_bufferCount = 0;
    m_pingPong = true;
    m_isAvailable = false;
    m_colorBufferCount = -1;
    m_vertexBufferIndex = -1;
    m_depthBufferIndex = -1;
    m_lastDestination = -1;
    m_activeBufferIndex = -1;
    m_drawBufferGroup = dbNone;
}


void FBO::CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType, bool isMRT) {
    BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
    bufferInfo.Init();
    if (bufferType == BufferInfo::btDepth)
        bufferInfo.m_attachment = GL_DEPTH_ATTACHMENT;
    else if (isMRT)
        bufferInfo.m_attachment = GL_COLOR_ATTACHMENT0 + attachmentIndex++;
    else
        bufferInfo.m_attachment = GL_COLOR_ATTACHMENT0; // color buffer for ping pong rendering; these will be bound alternatingly when needed as render target
    baseRenderer.ClearGLError();
    bufferInfo.m_handle = SharedTextureHandle();
    bufferInfo.m_handle.Claim();
    baseRenderer.CheckGLError("FBO::CreateBuffer->Claim");
    bufferInfo.m_type = bufferType;
    baseRenderer.ClearGLError();
    openGLStates.BindTexture2D(bufferInfo.m_handle, GL_TEXTURE0);
    baseRenderer.CheckGLError("FBO::CreateBuffer->BindTexture2D");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    switch (bufferType) {
        case BufferInfo::btColor:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width * m_scale, m_height * m_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            break;

        case BufferInfo::btVertex:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width * m_scale, m_height * m_scale, 0, GL_RGBA, GL_FLOAT, nullptr);
            break;

        default: // BufferInfo::btDepth
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_width * m_scale, m_height * m_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            break;
    }
    openGLStates.BindTexture2D(0, GL_TEXTURE0);
    ++m_bufferCount;
}


int FBO::CreateSpecialBuffers(BufferInfo::eBufferType bufferType, int& attachmentIndex, int bufferCount) {
    if (not bufferCount)
        return -1;
    for (int i = 0; i < bufferCount; ++i)
        CreateBuffer(m_bufferCount, attachmentIndex, bufferType, bufferType != BufferInfo::btDepth);
    return m_bufferCount - bufferCount;
}


bool FBO::DetachBuffer(int bufferIndex) {
    BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
    if (not bufferInfo.m_isAttached or (bufferInfo.m_attachment == GL_NONE))
        return true;
    BaseRenderer::ClearGLError();
    glFramebufferTexture2D(GL_FRAMEBUFFER, bufferInfo.m_attachment, GL_TEXTURE_2D, 0, 0);
    bufferInfo.m_isAttached = false;
    return BaseRenderer::CheckGLError("FBO::DetachBuffer");
}


bool FBO::AttachBuffer(int bufferIndex) {
    BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
    if (bufferInfo.m_isAttached or (bufferInfo.m_attachment == GL_NONE))
        return true;
    glFramebufferTexture2D(GL_FRAMEBUFFER, bufferInfo.m_attachment, GL_TEXTURE_2D, bufferInfo.m_handle, 0);
    return bufferInfo.m_isAttached = BaseRenderer::CheckGLError();
}


bool FBO::AttachBuffers(bool hasMRTs) {
    if (not m_handle.Claim())
        return false;
    BaseRenderer::ClearGLError();
    glBindFramebuffer(GL_FRAMEBUFFER, m_handle);
    BaseRenderer::CheckGLError();
    bool bindColorBuffers = true;
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    for (int i = 0; i < m_bufferCount; i++) {
#if 1
        if (m_bufferInfo[i].m_type == BufferInfo::btColor) { // always bind the first color buffer
            if (not bindColorBuffers)   // bind any others only if they are used as MRTs (and not for ping pong rendering)
                continue;
            bindColorBuffers = hasMRTs;
        }
#endif
#if 1
        AttachBuffer(i);
#else
        glFramebufferTexture2D(GL_FRAMEBUFFER, m_bufferInfo[i].m_attachment, GL_TEXTURE_2D, m_bufferInfo[i].m_handle, 0);
#endif
    }
    m_isAvailable = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (not m_isAvailable)
        baseRenderer.CheckGLError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return m_isAvailable;
}


void FBO::CreateRenderArea(void) {
    m_viewportArea.Setup(BaseQuad::defaultVertices[BaseQuad::voCenter], BaseQuad::defaultTexCoords[BaseQuad::tcRegular]);
    m_viewport = Viewport(0, 0, m_width * m_scale, m_height * m_scale);
}


bool FBO::Create(int width, int height, int scale, const FBOBufferParams& params) {
    if (width * height == 0)
        return false;
    m_handle = 0;
    m_width = width;
    m_height = height;
    m_scale = scale;
    m_bufferCount = 0;
    m_bufferInfo.Resize(params.colorBufferCount + params.vertexBufferCount + params.depthBufferCount);
    BaseRenderer::ClearGLError();
    int attachmentIndex = 0;
    for (int i = 0; i < params.colorBufferCount; i++) {
        CreateBuffer(i, attachmentIndex, BufferInfo::btColor, params.hasMRTs or (i == 0));
    }
    m_vertexBufferIndex = CreateSpecialBuffers(BufferInfo::btVertex, attachmentIndex, params.vertexBufferCount);
    // depth buffer must be created last or draw buffer management will fail as it relies on all draw buffers being stored in bufferInfo contiguously, starting at index 0
    m_depthBufferIndex = CreateSpecialBuffers(BufferInfo::btDepth, attachmentIndex, params.colorBufferCount ? params.depthBufferCount : 0);
    CreateRenderArea();
    if (not AttachBuffers(params.hasMRTs))
        return false;
    m_colorBufferCount = params.hasMRTs ? params.colorBufferCount : 1;
    m_vertexBufferCount = params.vertexBufferCount;
    m_drawBuffers.Resize(m_colorBufferCount + m_vertexBufferCount);
    m_name = params.name;
    return true;
}


void FBO::Destroy(void) {
    for (int i = 0; i < m_bufferCount; i++) {
        m_bufferInfo[i].m_handle.Release();
    }
    m_bufferInfo.Reset();
    m_handle.Release();
    //glDeleteFramebuffers(1, &m_handle);
}


bool FBO::SelectDrawBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup) {
    int l = m_drawBuffers.Length();
    if ((m_activeBufferIndex != bufferIndex) and (bufferIndex >= 0) and (bufferIndex < l)) {
        m_activeBufferIndex = bufferIndex;
        m_drawBufferGroup = dbSingle;
        m_drawBuffers[0] = m_bufferInfo[bufferIndex].m_attachment;
        for (int i = 1; i < l; i++)
            m_drawBuffers[i] = GL_NONE;
    }
    else if ((drawBufferGroup != dbCustom) and ((drawBufferGroup == dbNone) or (m_drawBufferGroup != drawBufferGroup))) {
        m_activeBufferIndex = -1;
        m_drawBufferGroup = (drawBufferGroup == dbNone) ? dbAll : drawBufferGroup;
        if (m_drawBufferGroup == dbAll) {
            for (int i = 0; i < l; ++i)
                m_drawBuffers[i] = m_bufferInfo[i].m_attachment;
        }
        else if (m_drawBufferGroup == dbColor) {
            int i = 0;
            for ( ; i < m_colorBufferCount; ++i) 
                m_drawBuffers[i] = m_bufferInfo[i].m_attachment;
            for ( ; i < l; ++i)
                m_drawBuffers[i] = GL_NONE;
        }
        else if (m_drawBufferGroup == dbExtra) {
            int i = 0;
            for (; i < m_colorBufferCount; ++i) 
                m_drawBuffers[i] = GL_NONE;
            for (; i < l; ++i)
                m_drawBuffers[i] = m_bufferInfo[i].m_attachment;
        }
    }
    return ReattachBuffers();
}


void FBO::SelectCustomDrawBuffers(DrawBufferList& drawBuffers) {
    m_drawBuffers = drawBuffers;
    m_activeBufferIndex = -1;
    m_drawBufferGroup = dbCustom;
}


// select draw buffer works in conjunction with Renderer::SetDrawBuffers
// The renderer keeps track of draw buffers and FBOs and stores those being temporarily overriden
// by other FBOs in a stack. Basically, the current OpenGL draw buffer is set using
// Renderer::SetDrawBuffers. However, when disabling a temporary render target (FBO), the 
// previous render target is automatically restored, which means calling its SetDrawBuffers
// function. To avoid FBO::SetDrawBuffers and Renderer::SetDrawBuffers looping forever,
// in that case, true is passed for reenable, causing SetDrawBuffers to directly call
// glDrawBuffers. The effect of that construction is that you can transparently nest 
// multiple FBO draw buffers.
bool FBO::SetDrawBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool reenable) {
    if (not SelectDrawBuffers(bufferIndex, drawBufferGroup))
        return false;
    openGLStates.BindTexture2D(0, GL_TEXTURE0);
    if (reenable)
        glDrawBuffers(m_drawBuffers.Length(), m_drawBuffers.Data());
    else
        baseRenderer.SetDrawBuffers(this, &m_drawBuffers);
    return true;
}


bool FBO::DepthBufferIsActive(int bufferIndex, eDrawBufferGroups drawBufferGroup) {
    if (m_depthBufferIndex < 0)
        return false;
    if (bufferIndex >= 0)
        return m_bufferInfo[bufferIndex].m_type == BufferInfo::btColor;
    return (drawBufferGroup == dbAll) or (drawBufferGroup == dbColor);
}


void FBO::Clear(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear) { // clear color has been set in Renderer.SetupOpenGL()
    if (clear) {
        baseRenderer.PushViewport();
        glViewport(0, 0, m_width * m_scale, m_height * m_scale);
        if (DepthBufferIsActive(bufferIndex, drawBufferGroup))
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        else
            glClear(GL_COLOR_BUFFER_BIT);
        baseRenderer.PopViewport();
    }
}


bool FBO::ReattachBuffers(void) {
    for (int i = 0; i < m_drawBuffers.Length(); i++)
        if (not AttachBuffer(i))
            return false;
    return true;
}


bool FBO::EnableBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable) {
    if (not SetDrawBuffers(bufferIndex, drawBufferGroup, reenable))
        return false;
    openGLStates.SetDepthTest(DepthBufferIsActive(bufferIndex, drawBufferGroup));
    Clear(bufferIndex, drawBufferGroup, clear);
    return baseRenderer.CheckGLError();
}


bool FBO::Enable(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable) {
    if (not m_isAvailable)
        return false;
    //BaseRenderer::ClearGLError();
    if (not IsEnabled()) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_handle);
        if (not baseRenderer.CheckGLError())
            return false;
        m_activeHandle = m_handle.get();
    }
    return EnableBuffers(bufferIndex, drawBufferGroup, clear, reenable);
}


void FBO::Disable(void) {
    if (IsEnabled()) {
        ReleaseBuffers();
        m_activeHandle = GL_NONE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        baseRenderer.RestoreDrawBuffer();
    }
}


bool FBO::BindBuffer(int bufferIndex, int tmuIndex) {
    if (bufferIndex < 0)
        return false;
    BaseRenderer::ClearGLError();
    if (tmuIndex < 0)
        tmuIndex = bufferIndex;
    for (int i = 0; i < m_bufferCount; ++i)
        if ((i != bufferIndex) and (m_bufferInfo[i].m_tmuIndex == tmuIndex))
            m_bufferInfo[i].m_tmuIndex = -1;
    openGLStates.BindTexture2D(m_bufferInfo[bufferIndex].m_handle, GL_TEXTURE0 + tmuIndex);
    baseRenderer.CheckGLError("FBO::BindBuffer");
    m_bufferInfo[bufferIndex].m_tmuIndex = tmuIndex;
    openGLStates.ActiveTexture(GL_TEXTURE0); // always reset!
    return true;
}


void FBO::ReleaseBuffers(void) {
    for (int i = 0; i < m_bufferCount; i++) {
        if (m_bufferInfo[i].m_tmuIndex >= 0) {
            Texture::Release<GL_TEXTURE_2D>(m_bufferInfo[i].m_tmuIndex);
            m_bufferInfo[i].m_tmuIndex = -1;
        }
    }
}


void FBO::SetViewport(bool flipVertically) noexcept {
    baseRenderer.SetViewport(m_viewport, GetWidth(true), GetHeight(true), flipVertically);
}


bool FBO::UpdateTransformation(const FBORenderParams& params) {
    bool haveTransformation = false;
    if (params.centerOrigin) {
        haveTransformation = true;
        baseRenderer.Translate(0.5, 0.5, 0);
    }
    if (params.rotation) {
        haveTransformation = true;
        baseRenderer.Rotate(params.rotation, 0, 0, 1);
    }
    if (params.flipVertically) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, params.scale * params.flipVertically, 1);
    }
    else if (params.source & 1) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, -params.scale, 1);
    }
    else if (params.scale != 1.0f) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, params.scale, 1);
    }
    return haveTransformation;
}


bool FBO::RenderTexture(Texture* source, const FBORenderParams& params, const RGBAColor& color) {
    Tristate<int> blending(-1, 0, 0);
    if (params.destination > -1) { // rendering to another FBO (than the main buffer)
        if (not Enable(params.destination, FBO::dbSingle, params.clearBuffer))
            return false;
        m_lastDestination = params.destination;
        blending = Tristate<int>(-1, 0, openGLStates.SetBlending(false));
    }
    else { // rendering to the current render target
        blending = Tristate<int>(-1, 0, openGLStates.SetBlending(true));
    }
    baseRenderer.PushMatrix();
    bool applyTransformation = UpdateTransformation(params);
    Tristate<GLenum> depthFunc(GL_NONE, GL_LEQUAL, openGLStates.DepthFunc(GL_ALWAYS));
    Tristate<int> faceCulling(-1, 1, openGLStates.SetFaceCulling(false));
    m_viewportArea.SetTexture(source);
    if (params.shader) {
        if (applyTransformation)
            params.shader->UpdateMatrices();
#if 1
        m_viewportArea.Render(params.shader, source);
#else
        if (params.centerOrigin)
            m_viewportArea.Render(params.shader, source);
        else
            m_viewportArea.Fill(ColorData::LightGreen);
#endif
    }
    else {
#ifdef _DEBUG
        static bool fillArea = not params.centerOrigin;
        static bool oscillate = false;
        static int i = 0;
        if (fillArea) {
            Viewport viewport = baseRenderer.Viewport();
            m_viewportArea.Fill(oscillate ? i ? ColorData::MediumBlue : ColorData::Orange : color);
            if (oscillate)
                i ^= 1;
        }
        else
#endif
        {
            if (params.premultiply)
                m_viewportArea.Premultiply();
            m_viewportArea.Render(color); // texture has been assigned to m_viewportArea above
        }
        //baseShaderHandler.StopShader();
    }
    openGLStates.SetBlending(blending);
    openGLStates.SetFaceCulling(faceCulling);
    openGLStates.DepthFunc(depthFunc);
    baseRenderer.PopMatrix();
    if (params.destination > -1)
        Disable();
    return true;
}


void FBO::Fill(RGBAColor color) {
    baseRenderer.Translate(0.5, 0.5, 0);
    m_viewportArea.Fill(static_cast<RGBColor>(color), color.A());
    baseRenderer.Translate(-0.5, -0.5, 0);
}


Texture* FBO::GetRenderTexture(const FBORenderParams& params) {
    if (params.source == params.destination)
        return nullptr;
    if (params.source < 0)
        m_renderTexture.m_handle = SharedTextureHandle(GLuint(-params.source));
    else
        m_renderTexture.m_handle = BufferHandle(params.source);
    m_renderTexture.HasBuffer() = true;
    return &m_renderTexture;
}


 // source < 0 means source contains a texture handle from some texture external to the FBO
bool FBO::Render(const FBORenderParams& params, const RGBAColor& color) {
    if (params.destination >= 0)
        m_lastDestination = params.destination;
    return RenderTexture((params.source == params.destination) ? nullptr : GetRenderTexture(params), params, color);
}


bool FBO::AutoRender(const FBORenderParams& params, const RGBAColor& color) {
    return Render({ .source = m_lastDestination, .destination = NextBuffer(m_lastDestination), .clearBuffer = params.clearBuffer, .scale = params.scale, .shader = params.shader }, color);
}


bool FBO::RenderToScreen(const FBORenderParams& params, const RGBAColor& color) {
    return Render(params, color);
}


// =================================================================================================

