#include "glew.h"
#include "conversions.hpp"
#include "rendertarget.h"
#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "tracy_wrapper.h"

GLint RenderTarget::m_activeHandle = GL_NONE;

// =================================================================================================

RenderTarget::RenderTarget() {
    Init();
}


void RenderTarget::Init(void) {
    m_handle = SharedFramebufferHandle(0);
    for (int i = 0; i < m_renderTextures.Length(); i++) {
        m_renderTextures[i].m_handle = SharedTextureHandle(0);
        m_renderTextures[i].Invalidate();
    }
    m_externalTexture.m_handle = SharedTextureHandle(0);
    m_externalTexture.Invalidate();
    m_depthTexture.m_handle = SharedTextureHandle(0);
    m_depthTexture.Invalidate();
    m_width = 0;
    m_height = 0;
    m_scale = 1;
    m_bufferCount = 0;
    m_colorBufferCount = -1;
    m_extraBufferIndex = -1;
    m_depthBufferIndex = -1;
    m_stencilBufferIndex = -1;
    m_hasStencil = false;
    m_computeBufferIndex = -1;
    m_computeBufferCount = 0;
    m_cubeMapIndex = -1;
    m_cubeMapCount = 0;
    m_cubeFace = 0;
    m_arrayLayerCount = 0;
    m_arrayLayer = 0;
    m_lastDestination = -1;
    m_activeBufferIndex = -1;
    m_pingPong = true;
    m_isAvailable = false;
    m_wasActivated = false;
    m_drawBufferGroup = dbNone;
    m_clearColor = ColorData::Invisible;
    m_bufferInfo.Reset();
    m_drawBuffers.Reset();
    m_customDrawBuffers.Reset();
    m_depthMode = dbmWrite;
}


void RenderTarget::CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType, bool isMRT) {
    BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
    bufferInfo.Init();
    if (bufferType == BufferInfo::btDepth)
        // With a stencil plane the single texture serves both attachment points at once, so it goes to
        // GL_DEPTH_STENCIL_ATTACHMENT. Attaching a separate GL_STENCIL_INDEX8 texture next to a depth
        // texture is what the old btStencil path did, and drivers are free to reject that combination
        // with GL_FRAMEBUFFER_UNSUPPORTED.
        bufferInfo.m_attachment = m_hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
    else if (bufferType == BufferInfo::btSkyMap)
        bufferInfo.m_attachment = GL_NONE; // compute-write target, not framebuffer-attached
    else
        bufferInfo.m_attachment = GL_COLOR_ATTACHMENT0 + attachmentIndex++;
    gfxStates.ClearError();
    bufferInfo.m_handle = SharedTextureHandle();
    bufferInfo.m_handle.Claim();
    bufferInfo.m_type = bufferType;
    // Only the COLOUR buffers become arrays - a depth buffer, a vertex buffer or a cube map has its own
    // shape and nothing to gain from layers.
    bufferInfo.m_isArray = (bufferType == BufferInfo::btColor) and (m_arrayLayerCount > 0);
    if (bufferType == BufferInfo::btCubemap) {
        // Six faces of one resource. The edge length is the target's WIDTH - a cube map is square by
        // definition, and taking the height as well would silently produce something that is not a cube.
        // Nearest filtering and clamp: a shadow cube map is compared, not interpolated, and filtering
        // across a face seam would compare against a blend of two directions.
        gfxStates.BindCubemap(bufferInfo.m_handle, 0);
        for (int face = 0; face < 6; ++face)
            glTexImage2D(GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face), 0, GLint(m_cubeMapFormat),
                         m_width * m_scale, m_width * m_scale, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
        gfxStates.BindCubemap(0, 0);
        gfxStates.CheckError();
        ++m_bufferCount;
        return;
    }
    if (bufferInfo.m_isArray) {
        // The whole stack in one allocation - unlike the cube map's six faces, which glTexImage2D can
        // only take one at a time. Every layer has the target's own size, so a layer is a full buffer.
        // Nearest and clamp like every colour buffer here: what renders into one is read back texel for
        // texel, and a filter tap at a layer's edge has nothing to reach into anyway.
        GLenum type = (m_colorFormat == GL_RGBA8) ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT;
        gfxStates.BindTexture(GL_TEXTURE_2D_ARRAY, bufferInfo.m_handle, 0);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GLint(m_colorFormat), m_width * m_scale, m_height * m_scale,
                     m_arrayLayerCount, 0, GL_RGBA, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
        gfxStates.BindTexture(GL_TEXTURE_2D_ARRAY, 0, 0);
        gfxStates.CheckError();
        ++m_bufferCount;
        return;
    }
    gfxStates.BindTexture2D(bufferInfo.m_handle, 0);
    if (bufferType == BufferInfo::btSkyMap) {
        // RGBA16F image for compute output. Linear sampling so the composit-PS can read it
        // without nearest-neighbor artifacts; clamp-to-edge to avoid wraparound at the seam of
        // the hemi-octahedral parameterization.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width * m_scale, m_height * m_scale, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gfxStates.BindTexture2D(0, 0);
        gfxStates.CheckError();
        ++m_bufferCount;
        return;
    }
    if (bufferType == BufferInfo::btDepth) {
        if (m_hasStencil)
            // 32F depth + 8 bit stencil. D24S8 would fit in half the memory, but it would also cost depth
            // precision against the plain D32F used everywhere else, and a stencil target is the one place
            // where that precision matters most (shadow volumes).
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH32F_STENCIL8, m_width * m_scale, m_height * m_scale, 0, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, m_width * m_scale, m_height * m_scale, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        // Sampling a packed depth/stencil texture returns the depth plane; say so explicitly instead of
        // relying on the default, because GetDepthAsTexture hands this handle straight to a sampler2D.
        if (m_hasStencil)
            glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_DEPTH_COMPONENT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else {
        if (bufferType == BufferInfo::btColor) {
            GLenum type = (m_colorFormat == GL_RGBA8) ? GL_UNSIGNED_BYTE : GL_HALF_FLOAT;
            glTexImage2D(GL_TEXTURE_2D, 0, m_colorFormat, m_width * m_scale, m_height * m_scale, 0, GL_RGBA, type, nullptr);
        }
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width * m_scale, m_height * m_scale, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }
    gfxStates.BindTexture2D(0, 0);
    gfxStates.CheckError();
    ++m_bufferCount;
}


int RenderTarget::CreateSpecialBuffers(BufferInfo::eBufferType bufferType, int& attachmentIndex, int bufferCount) {
#if 1
    if (not bufferCount)
        return -1;
#endif
    for (int i = 0; i < bufferCount; ++i)
        CreateBuffer(m_bufferCount, attachmentIndex, bufferType, bufferType != BufferInfo::btDepth);
    return m_bufferCount - bufferCount;
}


bool RenderTarget::DetachBuffer(int bufferIndex) {
    BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
    if (not bufferInfo.m_isAttached or (bufferInfo.m_boundAttachment == GL_NONE))
        return true;
    // An array layer was attached with glFramebufferTextureLayer, and only that call can take it off
    // again - glFramebufferTexture2D has no layer to name.
    if (bufferInfo.m_isArray)
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GLenum(bufferInfo.m_boundAttachment), 0, 0, 0);
    else
        glFramebufferTexture2D(GL_FRAMEBUFFER, bufferInfo.m_boundAttachment,
                               (bufferInfo.m_type == BufferInfo::btCubemap)
                               ? GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + m_cubeFace) : GL_TEXTURE_2D, 0, 0);
    bufferInfo.m_isAttached = false;
    bufferInfo.m_boundAttachment = GL_NONE;
    return true;
}


bool RenderTarget::AttachBuffer(int bufferIndex, int attachment) {
    BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
    if (bufferInfo.m_attachment == GL_NONE)  // compute write target, never framebuffer attached
        return true;
    if (attachment < 0) 
        attachment = bufferInfo.m_attachment;
    // Sitting on a different point than the one wanted (a dbSingle remap being set up or undone):
    // the old attachment has to go first, or it keeps the texture on a slot nobody accounts for.
    gfxStates.CheckError();
    if (bufferInfo.m_isAttached and (bufferInfo.m_boundAttachment != attachment))
        DetachBuffer(bufferIndex);
#ifndef _DEBUG
    if (bufferInfo.m_isAttached)
        return true;
#endif
    // The queue is emptied HERE so the check below judges this attach and nothing else. Without it any
    // error left standing by an earlier call - a shader that did not compile, a texture upload that was
    // refused - came back as "this buffer could not be attached", and the target was then reported
    // incomplete for a reason that has nothing to do with it.
    gfxStates.ClearError();
    if (bufferInfo.m_isArray) {
        gfxStates.ReleaseTexture(GL_TEXTURE_2D_ARRAY, bufferInfo.m_handle);
        gfxStates.CheckError();
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GLenum(attachment), bufferInfo.m_handle, 0, m_arrayLayer);
        gfxStates.CheckError();
    }
    else if (bufferInfo.m_type == BufferInfo::btCubemap) {
        gfxStates.ReleaseTexture(GL_TEXTURE_CUBE_MAP, bufferInfo.m_handle);
        gfxStates.CheckError();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GLenum (attachment), GLenum(GL_TEXTURE_CUBE_MAP_POSITIVE_X + m_cubeFace), bufferInfo.m_handle, 0);
        gfxStates.CheckError();
    }
    else {
        gfxStates.ReleaseTexture(GL_TEXTURE_2D, bufferInfo.m_handle);
        gfxStates.CheckError();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GLenum (attachment), GL_TEXTURE_2D, bufferInfo.m_handle, 0);
        gfxStates.CheckError();
    }
    bufferInfo.m_boundAttachment = attachment;
#ifdef _DEBUG
    return bufferInfo.m_isAttached = gfxStates.CheckError("RenderTarget::AttachBuffer");
#else
    return bufferInfo.m_isAttached = true;
#endif
}


void RenderTarget::ReleaseRemappedBuffers(void) {
    for (int i = 0; i < m_bufferCount; ++i)
        if (m_bufferInfo[i].m_isAttached and (m_bufferInfo[i].m_boundAttachment != m_bufferInfo[i].m_attachment))
            DetachBuffer(i);
}


bool RenderTarget::AttachBuffers(bool hasMRTs) {
    if (not m_handle.Claim())
        return false;
    gfxStates.ClearError();
    glBindFramebuffer(GL_FRAMEBUFFER, m_handle);
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
#ifdef _DEBUG
    if (not m_isAvailable)
        gfxStates.CheckError();
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return m_isAvailable;
}


// One face of a cube map buffer as the current draw target. The target has to be active - this only
// re-points the attachment, it does not bind the framebuffer.
//
// Six of these with a draw in between capture the surroundings of a point in every direction. Nothing
// else about the target changes, which is why this is a call of its own rather than a full Activate:
// re-activating six times would re-select draw buffers and depth state that have not changed.

bool RenderTarget::SelectCubeFace(int face, int bufferIndex) {
    if ((m_cubeMapCount <= 0) or (face < 0) or (face > 5))
        return false;
    int index = (bufferIndex < 0) ? m_cubeMapIndex : bufferIndex;
    if ((index < 0) or (index >= m_bufferCount) or (m_bufferInfo[index].m_type != BufferInfo::btCubemap))
        return false;
    if ((m_cubeFace == face) and m_bufferInfo[index].m_isAttached)
        return true;
    // The point the buffer currently sits on has to be kept: dbSingle moves a colour target onto
    // COLOR_ATTACHMENT0, and re-attaching to its own m_attachment instead would take the face off the
    // slot the draw buffer list names.
    int point = m_bufferInfo[index].m_isAttached ? m_bufferInfo[index].m_boundAttachment
                                                 : m_bufferInfo[index].m_attachment;

    m_cubeFace = face;
    // AttachBuffer reads m_cubeFace, so the detach has to happen first - it would otherwise leave the
    // previous face on the attachment point and re-attach onto itself.
    DetachBuffer(index);
    return AttachBuffer(index, point);
}


GLenum RenderTarget::BufferTarget(int bufferIndex) noexcept {
    if ((bufferIndex < 0) or (bufferIndex >= m_bufferCount))
        return GL_TEXTURE_2D;
    return m_bufferInfo[bufferIndex].m_isArray ? GLenum(GL_TEXTURE_2D_ARRAY)
           : (m_bufferInfo[bufferIndex].m_type == BufferInfo::btCubemap) ? GLenum(GL_TEXTURE_CUBE_MAP)
           : GLenum(GL_TEXTURE_2D);
}


bool RenderTarget::SelectArrayLayer(int layer) {
    if ((m_arrayLayerCount <= 0) or (layer < 0) or (layer >= m_arrayLayerCount))
        return false;
    if (m_arrayLayer == layer)
        return true;
    m_arrayLayer = layer;
    // Re-attaching needs this target's framebuffer bound. While it is not, setting the layer is the
    // whole job: Activate () attaches every buffer and reads m_arrayLayer while doing it. That is what
    // lets a caller pick the layer BEFORE activating, which is the only way to have the activation
    // clear the right one.
    //
    // IsEnabled (), not IsActive (): the question is whether the framebuffer is bound RIGHT NOW, and
    // that is what IsEnabled () asks GL. IsActive () asks the renderer's own draw buffer bookkeeping,
    // which knows nothing of a framebuffer bound by hand - as ReadBuffer () does.
    if (not IsEnabled())
        return true;
    bool ok = true;
    // EVERY colour buffer moves, so an MRT pass writes the same layer of all of them. Each keeps the
    // point it currently sits on: dbSingle may have moved one onto COLOR_ATTACHMENT0, and re-attaching
    // to its own m_attachment would take it off the slot the draw buffer list names.
    for (int i = 0; i < m_colorBufferCount; i++) {
        BufferInfo& bufferInfo = m_bufferInfo[i];
        if (not bufferInfo.m_isArray)
            continue;
        int point = bufferInfo.m_isAttached ? bufferInfo.m_boundAttachment : bufferInfo.m_attachment;
        // Attaching another layer to the SAME point replaces what sits there, so there is nothing to
        // detach first - and a detach would only clear the point for the attach to fill it again.
        // Clearing the flag is what gets AttachBuffer () past its "already attached" shortcut.
        bufferInfo.m_isAttached = false;
        ok = AttachBuffer(i, point) and ok;
    }
    return ok;
}


void RenderTarget::CreateRenderArea(void) {
    m_viewportArea.Setup(BaseQuadMesh::defaultVertices[BaseQuadMesh::voCenter], BaseQuadMesh::defaultTexCoords[BaseQuadMesh::tcRegular]);
    m_viewport = Viewport(0, 0, m_width * m_scale, m_height * m_scale);
}


bool RenderTarget::Create(int width, int height, int scale, const RTCreationParams& params) {
    if (width * height == 0)
        return false;
    GLint prevFramebuffer = GL_NONE;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFramebuffer);
    m_handle = SharedFramebufferHandle(0);
    m_width = width;
    m_height = height;
    m_scale = scale;
    m_bufferCount = 0;
    m_colorFormat = params.colorFormat;
    m_cubeMapFormat = params.cubeMapFormat;
    m_isScreenBuffer = params.isScreenBuffer;
    // Stencil is a plane of the depth buffer, not a buffer of its own (see m_stencilBufferIndex). Asking
    // for stencil without depth still yields one combined buffer.
    m_hasStencil = params.stencilBufferCount > 0;
    int depthBufferCount = m_hasStencil ? std::max(params.depthBufferCount, 1) : params.depthBufferCount;
    m_bufferInfo.Resize(params.colorBufferCount + params.vertexBufferCount + depthBufferCount + params.skyMapCount + params.cubeMapCount);
    // One sampling wrapper per colour buffer, dimensioned here and never again - see m_renderTextures.
    m_renderTextures.Resize(params.colorBufferCount);
    int attachmentIndex = 0;
    // Before the first buffer is made: CreateBuffer () reads it to decide what kind of texture to
    // allocate, and SelectArrayLayer () bounds against it.
    m_arrayLayerCount = params.arrayLayerCount;
    m_arrayLayer = 0;
    for (int i = 0; i < params.colorBufferCount; i++) {
        CreateBuffer(i, attachmentIndex, BufferInfo::btColor, params.hasMRTs or (i == 0));
        // The wrapper takes the buffer's handle right here - one wrapper per buffer means it never has
        // to be rehung, so GetAsTexture () only looks it up.
        m_renderTextures[i].m_handle = BufferHandle(i);
        m_renderTextures[i].m_type = BufferTarget(i);
        m_renderTextures[i].m_filtering = m_filtering;
        m_renderTextures[i].Invalidate();
    }

    m_extraBufferCount = params.vertexBufferCount;
    // extra buffers *must* be created right after any color buffers, or SelectDrawBuffers will not work correctly for dbExtra
    m_extraBufferIndex = CreateSpecialBuffers(BufferInfo::btVertex, attachmentIndex, params.vertexBufferCount);
    // depth buffer must be created last or draw buffer management will fail as it relies on all draw buffers being stored in bufferInfo contiguously, starting at index 0
    m_depthBufferIndex = CreateSpecialBuffers(BufferInfo::btDepth, attachmentIndex, depthBufferCount);
    m_stencilBufferIndex = m_hasStencil ? m_depthBufferIndex : -1;
    // Compute buffers come last so the existing color/vertex/depth-buffer iterations
    // (e.g. SelectDrawBuffers, which assumes m_bufferInfo[0..m_colorBufferCount-1] are color
    // buffers) remain valid. Caller addresses them via m_computeBufferIndex + slot.
    m_computeBufferIndex = (params.skyMapCount > 0) ? CreateSpecialBuffers(BufferInfo::btSkyMap, attachmentIndex, params.skyMapCount) : -1;
    m_computeBufferCount = params.skyMapCount;
    // Cube maps last, for the same reason as the compute buffers: everything that walks the colour
    // buffers assumes they sit contiguously from index 0.
    m_cubeMapIndex = (params.cubeMapCount > 0) ? CreateSpecialBuffers(BufferInfo::btCubemap, attachmentIndex, params.cubeMapCount) : -1;
    m_cubeMapCount = params.cubeMapCount;
    m_cubeFace = 0;
    CreateRenderArea();
    // Sky-map-only RTs (compute write target, no FBO attachments) skip AttachBuffers because
    // glCheckFramebufferStatus would return INCOMPLETE_MISSING_ATTACHMENT - the FBO is unused.
    if ((params.colorBufferCount > 0) || (depthBufferCount > 0) || (params.vertexBufferCount > 0) || (params.cubeMapCount > 0)) {
        if (not AttachBuffers(params.hasMRTs))
            return false;
    }
    else {
        m_isAvailable = true;
    }
    m_colorBufferCount = params.colorBufferCount;
    m_extraBufferCount = params.vertexBufferCount;
    m_drawBuffers.Resize(std::max(m_colorBufferCount, 1) + m_extraBufferCount);
    m_name = params.name;
    Disable();
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prevFramebuffer));
    gfxStates.CheckError();
    return true;
}


void RenderTarget::Destroy(void) {
    // The wrappers hold references to the buffer handles, so they go before the buffers do.
    m_renderTextures.Destroy();
    for (int i = 0; i < m_bufferCount; i++) {
        m_bufferInfo[i].m_handle.Release();
    }
    m_handle.Release();
    Init();
}


bool RenderTarget::SelectDrawBuffers(const RTActivationParams& params) {
    int l = m_drawBuffers.Length();

    // dbmReadOnly: the depth buffer is tested against but not written, so it may be sampled as a texture
    // in the same pass (soft particles / WBOIT). OpenGL has no read-only depth view -- turning depth (and
    // stencil) writes off is the equivalent, and it is what makes the feedback loop well-defined.
    // dbmWrite does NOT restore the write mask: every stage sets the states it needs (render state
    // contract), and the DX read-only DSV is just as one-way while it is bound.
    SetDepthMode(params.depthMode);

    // Leaving dbSingle: the buffer it had moved onto GL_COLOR_ATTACHMENT0 goes off that point first,
    // so restoring the canonical layout below does not attach buffer 0 to a slot the remapped buffer
    // still believes it owns (its detach would then tear buffer 0 off again). dbSingle does its own
    // bookkeeping - it detaches every other buffer anyway.
    if (params.drawBufferGroup != dbSingle)
        ReleaseRemappedBuffers();

    switch (params.drawBufferGroup) {
    case dbDepth:
        m_drawBufferGroup = dbDepth;
        for (int i = 0; i < l; ++i)
            m_drawBuffers[i] = GL_NONE;
        return true;

    case dbSingle:
        m_drawBufferGroup = dbSingle;
        if ((params.bufferIndex < 0) or (params.bufferIndex >= m_bufferInfo.Length()))
            return false;
        m_activeBufferIndex = params.bufferIndex;
        for (int i = 0; i < l; ++i) {
            if (i != params.bufferIndex)
                DetachBuffer(i);
            m_drawBuffers[i] = GL_NONE;
        }
        // DX and Vulkan make the selected buffer output slot 0 (RTV 0 / colour attachment 0), so a
        // shader with one output writes it no matter which buffer was picked. OpenGL cannot express
        // that through the draw buffer list: glDrawBuffers demands bufs [i] == COLOR_ATTACHMENTi or
        // NONE, so naming COLOR_ATTACHMENT2 in slot 0 is an INVALID_OPERATION, and naming it in slot 2
        // routes fragment output 2 - which a single output shader (layout (location = 0)) never writes.
        // The only way to put buffer k on output 0 is to attach its texture to COLOR_ATTACHMENT0.
        // A cube map face is a colour target like any other and takes the same route: it has to sit on
        // COLOR_ATTACHMENT0 to be reachable from fragment output 0. AttachBuffer () picks the face that
        // SelectCubeFace () last chose.
        if ((m_bufferInfo[params.bufferIndex].m_type == BufferInfo::btColor) or
            (m_bufferInfo[params.bufferIndex].m_type == BufferInfo::btCubemap)) {
            AttachBuffer(params.bufferIndex, int (GL_COLOR_ATTACHMENT0));
            m_drawBuffers[0] = GL_COLOR_ATTACHMENT0;
        }
        else {
            AttachBuffer(params.bufferIndex);
            if (params.bufferIndex < l)   // a depth buffer has no draw buffer slot
                m_drawBuffers[params.bufferIndex] = m_bufferInfo[params.bufferIndex].m_attachment;
        }
        return true;

    case dbAll:
        if ((m_drawBufferGroup == params.drawBufferGroup) and not params.reactivate)
            return true;
        [[fallthrough]];

    case dbNone:
        m_activeBufferIndex = -1;
        m_drawBufferGroup = dbAll;
        for (int i = 0; i < l; ++i) {
            m_drawBuffers[i] = m_bufferInfo[i].m_attachment;
            AttachBuffer(i);
        }
        return true;

    case dbColor:
        if ((m_drawBufferGroup != params.drawBufferGroup) or params.reactivate) {
            int i = 0;
            for (; i < m_colorBufferCount; ++i) {
                m_drawBuffers[i] = m_bufferInfo[i].m_attachment;
                AttachBuffer(i);
            }
            for (; i < l; ++i) {
                DetachBuffer(i);
                m_drawBuffers[i] = GL_NONE;
            }
        }
        return true;

    case dbExtra:
        if ((m_drawBufferGroup != params.drawBufferGroup) or params.reactivate) {
            int i = 0;
            for (; i < m_colorBufferCount; ++i) {
                DetachBuffer(i);
                m_drawBuffers[i] = GL_NONE;
            }
            for (int j = 0; j < m_extraBufferCount; ++i, ++j) {
                m_drawBuffers[i] = m_bufferInfo[i].m_attachment;
                AttachBuffer(i);
            }
        }
        return true;

    case dbCustom:
        // Re-apply the caller's own setup (Reactivate, or an Activate that keeps dbCustom).
        m_drawBufferGroup = dbCustom;
        ApplyCustomDrawBuffers();
        return true;

    default:
        return true;
    }
}


// Translate the API-neutral buffer-index list into GL attachment points: the buffer named for slot i is
// attached to GL_COLOR_ATTACHMENTi and drawn into from fragment output i, GL_NONE where the caller left a
// slot unused. Everything not named in the list is detached, so no leftover attachment of a previous
// group keeps receiving writes.
void RenderTarget::ApplyCustomDrawBuffers(void) {
    int slots = m_drawBuffers.Length();
    int listed = m_customDrawBuffers.Length();
    for (int i = 0; i < slots; ++i)
        m_drawBuffers[i] = GL_NONE;
    // Everything off first: slot i is served by GL_COLOR_ATTACHMENTi and nothing else (glDrawBuffers
    // demands bufs [i] == COLOR_ATTACHMENTi or NONE), so the listed buffers are attached to the point
    // their SLOT dictates, not to the one they own - and two of them may want to trade places.
    for (int i = 0; i < m_bufferCount; ++i) {
        BufferInfo::eBufferType type = m_bufferInfo[i].m_type;
        if ((type == BufferInfo::btColor) or (type == BufferInfo::btVertex))
            DetachBuffer(i);
    }
    for (int i = 0; (i < listed) and (i < slots); ++i) {
        int bufferIndex = m_customDrawBuffers[i];
        if ((bufferIndex < 0) or (bufferIndex >= m_bufferCount))
            continue;
        AttachBuffer(bufferIndex, int (GL_COLOR_ATTACHMENT0) + i);
        m_drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
    }
}


void RenderTarget::SelectCustomDrawBuffers(const CustomDrawBufferList& bufferIndices) {
    m_customDrawBuffers = bufferIndices;
    m_activeBufferIndex = -1;
    m_drawBufferGroup = dbCustom;
    ApplyCustomDrawBuffers();
}


bool RenderTarget::DepthBufferIsActive(int bufferIndex, eDrawBufferGroups drawBufferGroup) {
    // a shared depth source (SetDepthSource) is bound like an own depth buffer
    if ((m_depthBufferIndex < 0) and (m_depthSource == nullptr))
        return false;
    if (bufferIndex >= 0)
        return (m_bufferInfo[bufferIndex].m_type == BufferInfo::btColor) or (m_bufferInfo[bufferIndex].m_type == BufferInfo::btDepth);
    return (m_drawBufferGroup == dbAll) or (m_drawBufferGroup == dbColor) or (m_drawBufferGroup == dbDepth) or (m_drawBufferGroup == dbCustom);
}


// Attach the source's depth texture to this FBO's depth slot (persistent FBO state, so a one-time
// call covers all later Activates). Clear/ClearDepthBuffer keep gating on an OWN depth buffer
// (HaveDepthBuffer), so the foreign depth is never cleared through this target.
void RenderTarget::SetDepthSource(RenderTarget* source) {
    // The attachment point follows the buffer that is (or was) attached: a source whose depth carries a
    // stencil plane sits on GL_DEPTH_STENCIL_ATTACHMENT, and detaching must address the same point, or the
    // stencil plane stays attached. On detach the old source is the one that knows which point that was.
    RenderTarget* attached = (source != nullptr) ? source : m_depthSource;
    GLenum attachment = GL_DEPTH_ATTACHMENT;
    GLuint depthHandle = 0;
    if ((attached != nullptr) and (attached->m_depthBufferIndex >= 0)) {
        attachment = attached->m_bufferInfo[attached->m_depthBufferIndex].m_attachment;
        if (source != nullptr)
            depthHandle = attached->m_bufferInfo[attached->m_depthBufferIndex].m_handle;
    }
    m_depthSource = source;
    // Bind this FBO only to (re)attach its depth slot, then restore whatever framebuffer was bound
    // before. Hard-binding 0 here would desync the GL framebuffer binding from the DrawBufferHandler's
    // active draw buffer: a later *direct* SelectDrawBuffers on that still-"active" target then issues
    // glFramebufferTexture2D against the default framebuffer (FBO 0) -> GL_INVALID_OPERATION. SetDepthSource
    // must be transparent w.r.t. the framebuffer binding (e.g. DecalHandler::RenderStuckParticles calls
    // SetDepthSource(nullptr) while the scene buffer stays the active draw target).
    GLint prevFramebuffer = GL_NONE;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_handle);
    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, depthHandle, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prevFramebuffer));
}


void RenderTarget::Clear(const RTActivationParams& params) { // clear color has been set in Renderer.SetupGraphics()
    if (params.clear) {
        baseRenderer.PushViewport();
        gfxStates.SetViewport(0, 0, m_width * m_scale, m_height * m_scale);
        gfxStates.PushClearColor();
        gfxStates.SetClearColor(m_clearColor);
        if (DepthBufferIsActive(params.bufferIndex, params.drawBufferGroup) and (params.depthMode != dbmReadOnly))
            ClearDepthBuffer();
        if (m_colorBufferCount)
            ClearColorBuffers();
        gfxStates.PopClearColor();
        baseRenderer.PopViewport();
    }
}


bool RenderTarget::ReattachBuffers(void) {
    for (int i = 0; i < m_bufferInfo.Length(); i++)
        if (not AttachBuffer(i))
            return false;
    return true;
}


bool RenderTarget::EnableBuffers(const RTActivationParams& params) {
    if (not SelectDrawBuffers(params))
        return false;
#ifdef _DEBUG
    if (DepthBufferIsActive(params.bufferIndex, params.drawBufferGroup))
        gfxStates.SetDepthTest(true);
    else
        gfxStates.SetDepthTest(false);
#else
    gfxStates.SetDepthTest(DepthBufferIsActive(params.bufferIndex, params.drawBufferGroup));
#endif
#ifdef _DEBUG
    return gfxStates.CheckError();
#else
    return true;
#endif
}


bool RenderTarget::Enable(const RTActivationParams& params) {
    ZoneScoped;
    if (not m_isAvailable)
        return false;
    gfxStates.ClearError();
    if (not IsEnabled()) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_handle);
#ifdef _DEBUG
        if (not gfxStates.CheckError())
            return false;
#endif
    }
    if (not EnableBuffers(params))
        return false;
    m_isAvailable = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (not m_isAvailable)
        fprintf(stderr, "RenderTarget::Enable: Render target is incomplete\n");
    return true;
}


// The filter the colour buffer is read back with. Nothing else about a render target's sampling is
// negotiable - one level, no wrapping, no depth compare - but whether it is scaled or read texel for
// texel is the owner's business, not RenderTargetTexture::SetParams ()'s.

void RenderTarget::SetFiltering(GfxFilterMode filtering) {
    if (filtering == m_filtering)
        return;
    m_filtering = filtering;
    // Every wrapper that already exists - the filtering belongs to the target, not to one buffer. Ones
    // created later pick m_filtering up in GetRenderTexture ().
    // SetParams () writes GL state, so the texture has to be bound for it. Before the first
    // GetAsTexture () there is no buffer handle in it yet - and that call runs SetParams () itself,
    // so it picks the new filter up. Save/restore the binding the same way GetAsTexture () does.
    for (int i = 0; i < m_renderTextures.Length(); i++) {
        RenderTargetTexture& texture = m_renderTextures[i];
        texture.m_filtering = filtering;
        if (not texture.IsAvailable())
            continue;
        GLuint boundHandle = gfxStates.GetBoundTexture(GL_TEXTURE_2D, 0);
        texture.Activate(0);
        texture.SetParams(true);
        gfxStates.SetBoundTexture(GL_TEXTURE_2D, boundHandle, 0);
    }
    m_externalTexture.m_filtering = filtering;
}

// =================================================================================================

bool RenderTarget::IsActive(void) noexcept {
    return baseRenderer.IsActiveDrawBuffer(this);
}


#ifdef _DEBUG
bool RenderTarget::Activate(const RTActivationParams& params, const std::source_location& loc)
#else
bool RenderTarget::Activate(const RTActivationParams& params)
#   define loc
#endif
{
    ZoneScoped;
    if (not Enable(params))
        return false;
    baseRenderer.ActivateDrawBuffer(this);
    // Activate/Deactivate are a balanced viewport push/pop pair: Activate pushes the caller's
    // viewport, Deactivate's PopViewport restores it. A reactivation (via DeactivateDrawBuffer)
    // has no Deactivate of its own, so it must not push or set a viewport - the caller's
    // viewport is restored by the PopViewport immediately following in Deactivate().
    if (not (m_wasActivated or params.reactivate))
        baseRenderer.PushViewport(loc);
    SetViewport(true);
    Clear(params);
    m_wasActivated = true;
    return true;
}
#ifndef _DEBUG
#   undef loc
#endif


void RenderTarget::Disable(bool /*deactivate*/) noexcept {
    ZoneScoped;
    if (IsEnabled()) {
        ReleaseBuffers();
#if 1
        for (int i = 0; i < m_colorBufferCount; ++i) {
            m_drawBuffers[i] = GL_NONE;
            DetachBuffer(i);
        }
#endif
        m_activeBufferIndex = -1;
        m_drawBufferGroup = dbNone;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}


void RenderTarget::Deactivate(void) noexcept {
    baseRenderer.DeactivateDrawBuffer(this);
    baseRenderer.PopViewport();
    m_wasActivated = false;
}


bool RenderTarget::BindBuffer(int bufferIndex, int tmuIndex) {
    if (bufferIndex < 0)
        return false;
    gfxStates.ClearError();
    if (tmuIndex < 0)
        tmuIndex = bufferIndex;
    for (int i = 0; i < m_bufferCount; ++i)
        if ((i != bufferIndex) and (m_bufferInfo[i].m_tmuIndex == tmuIndex))
            m_bufferInfo[i].m_tmuIndex = -1;
    // A cube map is sampled by direction, so it has to go on the cube map target - binding it as 2D
    // would leave the sampler reading nothing.
    gfxStates.BindTexture(BufferTarget(bufferIndex), m_bufferInfo[bufferIndex].m_handle, tmuIndex);
    m_bufferInfo[bufferIndex].m_tmuIndex = tmuIndex;
    return true;
}


void RenderTarget::ReleaseBuffers(void) {
    for (int i = 0; i < m_bufferCount; i++) {
        gfxStates.ReleaseTexture(BufferTarget(i), m_bufferInfo[i].m_handle);
        m_bufferInfo[i].m_tmuIndex = -1;
    }
}


void RenderTarget::SetViewport(bool flipVertically) noexcept {
    baseRenderer.SetViewport(m_viewport, GetWidth(true), GetHeight(true), flipVertically);
}


bool RenderTarget::UpdateTransformation(const RTRenderParams& params) {
    bool haveTransformation = false;
    if (params.centerOrigin) {
        haveTransformation = true;
        baseRenderer.Translate(0.5, 0.5, 0);
    }
    if (params.rotation) {
        haveTransformation = true;
        baseRenderer.Rotate(params.rotation, 0, 0, 1);
    }
#if 1
    if (params.flipVertically) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, params.scale * params.flipVertically, 1);
    }
    else if (params.source & 1) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, -params.scale, 1);
    }
#endif
    else if (params.scale != 1.0f) {
        haveTransformation = true;
        baseRenderer.Scale(params.scale, params.scale, 1);
    }
    return haveTransformation;
}


bool RenderTarget::RenderAsTexture(Texture* source, const RTRenderParams& params, const RGBAColor& color) {
    bool deactivate = false;
    if (params.destination >= 0) {
        deactivate = not IsActive();
        if (not Activate({ .bufferIndex = params.destination, .drawBufferGroup = RenderTarget::dbSingle, .clear = params.clearBuffer, .reactivate = not deactivate }))
            return false;
        m_lastDestination = params.destination;
    }
    baseRenderer.PushMatrix();
    bool applyTransformation = UpdateTransformation(params);
    gfxStates.DepthFunc(GfxOperations::CompareFunc::Always);
    gfxStates.SetFaceCulling(0);
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
        bool fillArea = false; // params.source > 0;
        static bool oscillate = false;
        static int i = 0;
        if (fillArea) {
            Viewport viewport = baseRenderer.GetViewport();
            m_viewportArea.Fill(oscillate ? i ? ColorData::MediumBlue : ColorData::Orange : color);
            if (oscillate)
                i ^= 1;
        }
        else
#endif
        {
            if (params.premultiply)
                m_viewportArea.Premultiply();
            // DX/VK parity: the no-shader composite sets its own 2D states (blending on for
            // screen composites, off for buffer-to-buffer) instead of inheriting the caller's state
            baseRenderer.Set2DRenderStates(params.destination < 0);
            //m_viewportArea.SetTransformations({ .flipVertically = params.flipVertically == 1 });
            m_viewportArea.Render(nullptr, source, color); // texture has been assigned to m_viewportArea above
        }
    }
    baseRenderer.PopMatrix();
    if (deactivate)
        Deactivate();
    return true;
}


void RenderTarget::Fill(RGBAColor color) {
    baseRenderer.Translate(0.5, 0.5, 0);
    m_viewportArea.Fill(static_cast<RGBColor>(color), color.A());
    baseRenderer.Translate(-0.5, -0.5, 0);
}


Texture* RenderTarget::GetAsTexture(const RTRenderParams& params, int tmuIndex) {
    if (params.source == params.destination)
        return nullptr;
    // A negative source is a texture handle from OUTSIDE this target. It must not be wrapped as an
    // owned handle: the wrapper would delete the texture when the render texture is next pointed
    // elsewhere - which used to take a colour buffer of the caller's frame with it.
    SharedTextureHandle handle =
        (params.source < 0)
        ? SharedTextureHandle(GLuint(-params.source), false)
        : BufferHandle(params.source);
    //DetachBuffer((params.source < 0) ? -params.source : params.source);
    RenderTargetTexture* texture = (params.source < 0) ? &m_externalTexture : GetRenderTexture(params.source);
    if (texture == nullptr)
        return nullptr;
    // A colour buffer's wrapper got its handle in Create () and keeps it - only the external one is ever
    // pointed somewhere new here. m_hasParams is what still brings a wrapper to apply its sampler
    // parameters once: with the handle already in place there is no change left to trigger it.
    bool bChanged = (texture->m_handle != handle);
    if (bChanged)
        texture->m_handle = handle;
    texture->Validate();
    if ((bChanged or not texture->m_hasParams) and (tmuIndex > -1)) {
        GLuint boundHandle = gfxStates.GetBoundTexture(GL_TEXTURE_2D, tmuIndex);
        texture->Activate(tmuIndex);
        texture->SetParams(true);
        gfxStates.SetBoundTexture(GL_TEXTURE_2D, boundHandle, tmuIndex);
    }
    return texture;
}


// One wrapper per colour buffer. The array is dimensioned in Create () and never grows, so the address
// handed out here stays valid for as long as the target does.

RenderTargetTexture* RenderTarget::GetRenderTexture(int bufferIndex) noexcept {
    return ((bufferIndex >= 0) and (bufferIndex < m_renderTextures.Length())) ? &m_renderTextures[bufferIndex] : nullptr;
}


Texture* RenderTarget::GetDepthAsTexture(void) {
    SharedTextureHandle handle = BufferHandle(m_depthBufferIndex);
    m_depthTexture.Validate();
    if (m_depthTexture.m_handle != handle) {
        m_depthTexture.m_handle = handle;
#if 1
        m_depthTexture.Bind(0);
        m_depthTexture.SetParams(true);
        //m_depthTexture.Release();
#endif
    }
    return &m_depthTexture;
}


Texture* RenderTarget::GetDepthAsShadowTexture(void) {
    SharedTextureHandle handle = BufferHandle(m_depthBufferIndex);
    m_shadowTexture.Validate();
    if (m_shadowTexture.m_handle != handle) {
        m_shadowTexture.m_handle = handle;
#if 1
        m_shadowTexture.Bind(0);
        m_shadowTexture.SetParams(true);
        //m_depthTexture.Release();
#endif
    }
    return &m_shadowTexture;
}


// source < 0 means source contains a texture handle from some texture external to the RenderTarget
bool RenderTarget::Render(const RTRenderParams& params, const RGBAColor& color) {
    ZoneScoped;
    if (params.destination >= 0)
        m_lastDestination = params.destination;
    return RenderAsTexture((params.source == params.destination) ? nullptr : GetAsTexture(params), params, color);
}


bool RenderTarget::AutoRender(const RTRenderParams& params, const RGBAColor& color) {
    return Render({ .source = m_lastDestination, .destination = NextBuffer(m_lastDestination), .clearBuffer = params.clearBuffer, .scale = params.scale, .shader = params.shader }, color);
}

// =================================================================================================
// Reading a colour buffer back to the CPU.
//
// The external format and type have to match the internal one: glGetTexImage converts, but only
// between things that fit, and a packed format (R11F_G11F_B10F) has to be read as the packed word it
// is - one uint32 per texel, not three floats.

static bool ColorReadFormat(GLenum internalFormat, GLenum& format, GLenum& type, size_t& texelBytes) {
    switch (internalFormat) {
        case GL_RGBA8:
            format = GL_RGBA; type = GL_UNSIGNED_BYTE; texelBytes = 4; return true;
        case GL_RGBA16F:
            format = GL_RGBA; type = GL_HALF_FLOAT; texelBytes = 8; return true;
        case GL_RGBA32F:
            format = GL_RGBA; type = GL_FLOAT; texelBytes = 16; return true;
        case GL_R16F:
            format = GL_RED; type = GL_HALF_FLOAT; texelBytes = 2; return true;
        case GL_R32F:
            format = GL_RED; type = GL_FLOAT; texelBytes = 4; return true;
        case GL_R11F_G11F_B10F:
            format = GL_RGB; type = GL_UNSIGNED_INT_10F_11F_11F_REV; texelBytes = 4; return true;
    }
    return false;
}


size_t RenderTarget::BufferSize(int bufferIndex) {
    if ((bufferIndex < 0) or (bufferIndex >= m_colorBufferCount))
        return 0;

    GLenum format, type;
    size_t texelBytes;

    if (not ColorReadFormat(m_colorFormat, format, type, texelBytes))
        return 0;
    return size_t(GetWidth(true)) * size_t(GetHeight(true)) * texelBytes;
}


bool RenderTarget::ReadBuffer(int bufferIndex, void* buffer, size_t bufferSize, int arraySlice) {
    if (not (buffer and m_isAvailable))
        return false;
    if ((bufferIndex < 0) or (bufferIndex >= m_colorBufferCount))
        return false;
    if (m_bufferInfo[bufferIndex].m_isArray and ((arraySlice < 0) or (arraySlice >= m_arrayLayerCount)))
        return false;

    GLenum format, type;
    size_t texelBytes;

    if (not ColorReadFormat(m_colorFormat, format, type, texelBytes))
        return false;

    size_t needed = size_t(GetWidth(true)) * size_t(GetHeight(true)) * texelBytes;

    if (bufferSize < needed)
        return false;
    gfxStates.ClearError();
    if (m_bufferInfo[bufferIndex].m_isArray) {
        // glGetTexImage on an array hands out EVERY layer at once, and there is no single layer form of
        // it before GL 4.5. Reading through the framebuffer instead gets one layer without a temporary
        // the size of the whole stack: attaching a layer is what this class does anyway.
        GLint prevFramebuffer = 0, prevReadBuffer = 0;

        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFramebuffer);
        glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);

        BufferInfo& bufferInfo = m_bufferInfo[bufferIndex];
        // Attached HERE rather than through SelectArrayLayer (): that one returns at once when the layer
        // asked for is already selected, and this buffer may still be detached all the same - a dbSingle
        // pass takes every other colour buffer off its point, and the bake is exactly such a pass.
        int point = (bufferInfo.m_boundAttachment != GL_NONE) ? bufferInfo.m_boundAttachment : bufferInfo.m_attachment;
        int prevLayer = m_arrayLayer;

        glBindFramebuffer(GL_FRAMEBUFFER, m_handle);
        m_arrayLayer = arraySlice;
        bufferInfo.m_isAttached = false;   // gets AttachBuffer () past its "already attached" shortcut
        AttachBuffer(bufferIndex, point);
        glReadBuffer(GLenum(point));
        glReadPixels(0, 0, GetWidth(true), GetHeight(true), format, type, buffer);

        bool ok = gfxStates.CheckError();

        glReadBuffer(GLenum(prevReadBuffer));
        // Only the number goes back - the next Activate () attaches every buffer and reads it while
        // doing so, and re-attaching here would put this one back onto a point the caller may not want.
        m_arrayLayer = prevLayer;
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prevFramebuffer));
        return ok;
    }
    // Bound past the TMU bookkeeping and put back afterwards: this is a read, not a binding the
    // renderer is meant to keep - leaving the atlas on a texture unit would outlive the call.
    int boundTexture = gfxStates.GetBoundTexture(GL_TEXTURE_2D, 0);

    gfxStates.BindTexture(GL_TEXTURE_2D, m_bufferInfo[bufferIndex].m_handle, 0);
    glGetTexImage(GL_TEXTURE_2D, 0, format, type, buffer);

    bool ok = gfxStates.CheckError();

    gfxStates.BindTexture(GL_TEXTURE_2D, GLuint(boundTexture), 0);
    return ok;
}

// =================================================================================================

bool RenderTarget::WriteBuffer(int bufferIndex, const void* data, size_t dataSize, int arraySlice) {
    if (not (data and m_isAvailable))
        return false;
    if ((bufferIndex < 0) or (bufferIndex >= m_colorBufferCount))
        return false;
    if (m_bufferInfo[bufferIndex].m_isArray and ((arraySlice < 0) or (arraySlice >= m_arrayLayerCount)))
        return false;

    GLenum format, type;
    size_t texelBytes;

    if (not ColorReadFormat(m_colorFormat, format, type, texelBytes))
        return false;

    size_t needed = size_t(GetWidth(true)) * size_t(GetHeight(true)) * texelBytes;

    if (dataSize < needed)
        return false;
    gfxStates.ClearError();

    GLenum target = BufferTarget(bufferIndex);
    int boundTexture = gfxStates.GetBoundTexture(target, 0);

    gfxStates.BindTexture(target, m_bufferInfo[bufferIndex].m_handle, 0);
    if (m_bufferInfo[bufferIndex].m_isArray)
        // One layer of the stack - the depth of the region is 1, its z offset the slice.
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, arraySlice, GetWidth(true), GetHeight(true), 1, format, type, data);
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GetWidth(true), GetHeight(true), format, type, data);

    bool ok = gfxStates.CheckError();

    gfxStates.BindTexture(target, GLuint(boundTexture), 0);
    return ok;
}

// =================================================================================================
