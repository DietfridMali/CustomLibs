#include "glew.h"
#include "fbo.h"
#include "base_renderer.h"
#include "base_shaderhandler.h"

// =================================================================================================

#define INVALID_BUFFER_INDEX 0x7FFFFFFF // (max int32_t)

FBO::FBO() {
    m_handle = 0;
    m_width = 0;
    m_height = 0;
    m_scale = 1;
    m_bufferCount = 0;
    m_pingPong = true;
    m_isAvailable = false;
    m_isEnabled = false;
    m_vertexBufferIndex = -1;
    m_depthBufferIndex = -1;
    m_lastDestination = -1;
    m_lastBufferIndex = INVALID_BUFFER_INDEX; // some invalid drawbuffer id value
    m_viewportSave = nullptr;
}


void FBO::CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType, bool isMRT) {
    BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
    if (bufferType == BufferInfo::btDepth)
        bufferInfo.m_attachment = GL_DEPTH_ATTACHMENT;
    else if (isMRT)
        bufferInfo.m_attachment = GL_COLOR_ATTACHMENT0 + attachmentIndex++;
    else
        bufferInfo.m_attachment = GL_COLOR_ATTACHMENT0; // color buffer for ping pong rendering; these will be bound alternatingly when needed as render target
    bufferInfo.m_handle = SharedTextureHandle();
    bufferInfo.m_handle.Claim();
    bufferInfo.m_type = bufferType;
    glBindTexture(GL_TEXTURE_2D, bufferInfo.m_handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    switch (bufferType) {
        case BufferInfo::btColor:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width * m_scale, m_height * m_scale, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            break;

        case BufferInfo::btVertex:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width * m_scale, m_height * m_scale, 0, GL_RGBA, GL_FLOAT, nullptr);
            break;

        default: // BufferInfo::btDepth
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_width * m_scale, m_height * m_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            break;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
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
    glFramebufferTexture2D(GL_FRAMEBUFFER, bufferInfo.m_attachment, GL_TEXTURE_2D, 0, 0);
    bufferInfo.m_isAttached = false;
    return BaseRenderer::CheckGLError();
}


bool FBO::AttachBuffer(int bufferIndex) {
    BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
    if (bufferInfo.m_isAttached or (bufferInfo.m_attachment == GL_NONE))
        return true;
    glFramebufferTexture2D(GL_FRAMEBUFFER, bufferInfo.m_attachment, GL_TEXTURE_2D, bufferInfo.m_handle, 0);
    return bufferInfo.m_isAttached = BaseRenderer::CheckGLError();
}


bool FBO::ReattachBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup) {
    if (bufferIndex >= 0)
        return AttachBuffer(bufferIndex);
    for (int i = 0; i < m_drawBuffers[drawBufferGroup].Length(); i++)
        if (not AttachBuffer(i))
            return false;
    return true;
}


bool FBO::AttachBuffers(bool hasMRTs) {
    if (not m_handle.Claim())
        return false;
    BaseRenderer::ClearGLError();
    glBindFramebuffer(GL_FRAMEBUFFER, m_handle);
    BaseRenderer::CheckGLError();
    bool bindColorBuffers = true;
    for (int i = 0; i < m_bufferCount; i++) {
#if 1
        if (m_bufferInfo[i].m_type == BufferInfo::btColor) { // always bind the first color buffer
            if (not bindColorBuffers)   // bind any others only if they are used as MRTs (and not for ping pong rendering)
                continue;
            bindColorBuffers = hasMRTs;
        }
#endif
        AttachBuffer(i);
        glFramebufferTexture2D(GL_FRAMEBUFFER, m_bufferInfo[i].m_attachment, GL_TEXTURE_2D, m_bufferInfo[i].m_handle, 0);
    }
    m_isAvailable = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return m_isAvailable;
}


void FBO::CreateRenderArea(void) {
    m_viewportArea.Setup({ Vector3f{ -0.5f, -0.5f, 0.0f }, Vector3f{ -0.5f, 0.5f, 0.0f }, Vector3f{ 0.5f, 0.5f, 0.0f }, Vector3f{ 0.5f, -0.5f, 0.0f }},
                         { /*TexCoord{0, 0}, TexCoord{0, 1}, TexCoord{1, 1}, TexCoord{1, 0} */});
    m_viewport = Viewport(0, 0, m_width * m_scale, m_height * m_scale);
}


bool FBO::Create(int width, int height, int scale, const FBOBufferParams& params) {
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
    m_depthBufferIndex = CreateSpecialBuffers(BufferInfo::btDepth, attachmentIndex, params.colorBufferCount ? params.depthBufferCount : 0);
    CreateRenderArea();
    if (not AttachBuffers(params.hasMRTs))
        return false;
    int vertexBufferOffset = params.hasMRTs ? params.colorBufferCount : 1;
    m_drawBuffers[dbAll].Resize(vertexBufferOffset + params.vertexBufferCount);
    m_drawBuffers[dbColor].Resize(vertexBufferOffset + params.vertexBufferCount);
    m_drawBuffers[dbExtra].Resize(vertexBufferOffset + params.vertexBufferCount);
    m_drawBuffers[dbSingle].Resize(1);
    for (int i = 0; i < vertexBufferOffset; i++) {
        m_drawBuffers[dbAll][i] = 
        m_drawBuffers[dbColor][i] = m_bufferInfo[i].m_attachment;
        m_drawBuffers[dbExtra][i] = GL_NONE;
    }
    for (int i = 0; i < params.vertexBufferCount; i++) {
        m_drawBuffers[dbAll][vertexBufferOffset + i] =
        m_drawBuffers[dbExtra][vertexBufferOffset + i] = m_bufferInfo[m_vertexBufferIndex + i].m_attachment;
        m_drawBuffers[dbColor][vertexBufferOffset + i] = GL_NONE;
    }
    m_name = params.name;
    return true;
}


void FBO::Destroy(void) {
    for (int i = 0; i < m_bufferCount; i++) {
        m_bufferInfo[i].m_handle.Release();
    }
    m_handle.Release();
    //glDeleteFramebuffers(1, &m_handle);
}


// select draw buffer works in conjunction with Renderer::SetDrawBuffers
// The renderer keeps track of draw buffers and FBOs and stores those being temporarily overriden
// by other FBOs in a stack. Basically, the current OpenGL draw buffer is set using
// Renderer::SetDrawBuffers. However, when disabling a temporary render target (FBO), the 
// previous render target is automatically restored, which means calling its SelectDrawBuffers
// function. To avoid FBO::SelectDrawBuffers and Renderer::SetDrawBuffers looping forever,
// in that case, true is passed for reenable, causing SelectDrawBuffers to directly call
// glDrawBuffers. The effect of that construction is that you can transparently nest 
// multiple FBO draw buffers.
void FBO::SelectDrawBuffers(int bufferIndex, bool reenable, eDrawBufferGroups drawBufferGroup) {
    glBindTexture(GL_TEXTURE_2D, 0);
    DrawBufferList& drawBuffers = (bufferIndex >= 0) ? m_drawBuffers[dbSingle] : m_drawBuffers[drawBufferGroup];
    if (bufferIndex >= 0)
        drawBuffers[0] = m_bufferInfo[bufferIndex].m_attachment;
    if (reenable)
        glDrawBuffers(drawBuffers.Length(), drawBuffers.Data());
    else
        baseRenderer.SetDrawBuffers(this, &drawBuffers);
}


bool FBO::DepthBufferIsActive(int bufferIndex, eDrawBufferGroups drawBufferGroup) {
    if (m_depthBufferIndex < 0)
        return false;
    if (bufferIndex >= 0)
        return m_bufferInfo[bufferIndex].m_type == BufferInfo::btColor;
    return (drawBufferGroup == dbAll) or (drawBufferGroup == dbColor);
}


void FBO::Clear(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clearBuffer) { // clear color has been set in Renderer.SetupOpenGL()
    if (clearBuffer) {
        glViewport(0, 0, m_width * m_scale, m_height * m_scale);
        if (DepthBufferIsActive(bufferIndex, drawBufferGroup))
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        else
            glClear(GL_COLOR_BUFFER_BIT);
    }
}


bool FBO::EnableBuffer(int bufferIndex, bool clearBuffer, bool reenable, eDrawBufferGroups drawBufferGroup) {
    if (not ReattachBuffers(bufferIndex, drawBufferGroup))
        return false;
    SelectDrawBuffers(bufferIndex, reenable);
    if (m_depthBufferIndex >= 0)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    Clear(bufferIndex, drawBufferGroup, clearBuffer);
    m_lastBufferIndex = bufferIndex;
    return baseRenderer.CheckGLError();
}


bool FBO::Enable(int bufferIndex, bool clearBuffer, bool reenable, eDrawBufferGroups drawBufferGroup) {
    if (not m_isAvailable)
        return false;
    if (not reenable and (bufferIndex == m_lastBufferIndex))
        return true;
    //BaseRenderer::ClearGLError();
    if (bufferIndex == INVALID_BUFFER_INDEX)
        Disable();
    else {
        glBindFramebuffer(GL_FRAMEBUFFER, m_handle);
        if (not (m_isEnabled = baseRenderer.CheckGLError()))
            return false;
        if (not EnableBuffer(bufferIndex, clearBuffer, reenable, drawBufferGroup))
            return false;
    }
    return m_isEnabled;
}


void FBO::Disable(void) {
    if (m_lastBufferIndex != INVALID_BUFFER_INDEX) {
        m_lastBufferIndex = INVALID_BUFFER_INDEX;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ReleaseBuffers();
        baseRenderer.RestoreDrawBuffer();
        m_isEnabled = false;
    }
}


bool FBO::BindBuffer(int bufferIndex, int tmuIndex) {
    if (bufferIndex < 0)
        return false;
    BaseRenderer::ClearGLError();
    glEnable(GL_TEXTURE_2D);
    if (tmuIndex < 0)
        tmuIndex = bufferIndex;
    for (int i = 0; i < m_bufferCount; ++i)
        if ((i != bufferIndex) and (m_bufferInfo[i].m_tmuIndex == tmuIndex))
            m_bufferInfo[i].m_tmuIndex = -1;
    glActiveTexture(GL_TEXTURE0 + tmuIndex);
    glBindTexture(GL_TEXTURE_2D, m_bufferInfo[bufferIndex].m_handle);
    baseRenderer.CheckGLError("FBO::BindBuffer");
    m_bufferInfo[bufferIndex].m_tmuIndex = tmuIndex;
    glActiveTexture(GL_TEXTURE0); // always reset!
    return true;
}


void FBO::ReleaseBuffers(void) {
    for (int i = 0; i < m_bufferCount; i++) {
        if (m_bufferInfo[i].m_tmuIndex >= 0) {
            Texture::Release(m_bufferInfo[i].m_tmuIndex);
            m_bufferInfo[i].m_tmuIndex = -1;
        }
    }
}


void FBO::SetViewport(void) {
    baseRenderer.SetViewport(m_viewport, false);
}


bool FBO::RenderTexture(Texture* source, const FBORenderParams& params, const RGBAColor& color) {
    if (params.destination > -1) { // rendering to another FBO (than the main buffer)
        if (not Enable(params.destination, params.clearBuffer))
            return false;
        m_lastDestination = params.destination;
        glDisable(GL_BLEND);
    }
    else { // rendering to the current render target
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    baseRenderer.PushMatrix();
    baseRenderer.Translate(0.5, 0.5, 0);
    if (params.rotation)
        baseRenderer.Rotate(params.rotation, 0, 0, 1);
    if (params.flipVertically)
        baseRenderer.Scale(params.scale, params.scale * params.flipVertically, 1);
    else if (params.source & 1)
        baseRenderer.Scale(params.scale, -params.scale, 1);
    else if (params.scale != 1.0f)
        baseRenderer.Scale(params.scale, params.scale, 1);
    glDepthFunc(GL_ALWAYS);
    glDisable(GL_CULL_FACE);
    m_viewportArea.SetTexture(source);
    if (params.shader)
        m_viewportArea.Render(params.shader, source);
    else {
#ifdef _DEBUG
        static bool fillArea = false;
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
            m_viewportArea.Render(color); // texture has been assigned to m_viewportArea above
        //baseShaderHandler.StopShader();
    }
    glEnable(GL_CULL_FACE);
    glDepthFunc(GL_LESS);
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


 // source < 0 means source contains a texture handle from some texture external to the FBO
bool FBO::Render(const FBORenderParams& params, const RGBAColor& color) {
    Texture texture;
    if (params.source != params.destination) {
        if (params.source < 0)
            texture.m_handle = SharedTextureHandle(GLuint(-params.source));
        else
            texture.m_handle = BufferHandle(params.source);
        texture.HasBuffer() = true;
    }
    if (params.destination >= 0)
        m_lastDestination = params.destination;
    return RenderTexture((params.source == params.destination) ? nullptr : &texture, params, color);
    }


bool FBO::AutoRender(const FBORenderParams& params, const RGBAColor& color) {
    return Render({ .source = m_lastDestination, .destination = NextBuffer(m_lastDestination), .clearBuffer = params.clearBuffer, .scale = params.scale, .shader = params.shader }, color);
}


bool FBO::RenderToScreen(const FBORenderParams& params, const RGBAColor& color) {
    return Render(params, color);
}


// =================================================================================================

