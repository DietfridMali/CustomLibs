#include "readtarget.h"
#include "rendertarget.h"

// =================================================================================================

void GfxReadTarget::DeleteFence(void) noexcept {
    if (m_fence) {
        glDeleteSync(m_fence);
        m_fence = nullptr;
    }
}


bool GfxReadTarget::Request(RenderTarget& renderTarget, int bufferIndex, int arraySlice) {
    if (not IsIdle())
        return false;
    return renderTarget.ReadBufferAsync(bufferIndex, *this, arraySlice);
}


bool GfxReadTarget::Allocate(size_t size) {
    if (size == 0)
        return false;
    if (m_handle == 0) {
        glGenBuffers(1, &m_handle);
        if (m_handle == 0)
            return false;
        m_capacity = 0;
    }
    if (m_capacity == size)
        return true;

    GLint prevBuffer = 0;

    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevBuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_handle);
    glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(size), nullptr, GL_STREAM_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(prevBuffer));
    m_capacity = size;
    return true;
}


bool GfxReadTarget::Submit(size_t size, int width, int height) {
    DeleteFence();
    m_fence = glFenceSync(GL_SYNC_FENCE, 0);
    if (not m_fence)
        return false;
    SetRequest(size, width, height, 0, 0);
    return true;
}


BaseReadTarget::State GfxReadTarget::Poll(void) {
    if (m_state != State::Pending)
        return m_state;

    GLenum result = glClientWaitSync(m_fence, 0, 0);

    if (result == GL_TIMEOUT_EXPIRED)
        return m_state;
    DeleteFence();
    if (result == GL_WAIT_FAILED) {
        m_state = State::Idle;
        return m_state;
    }

    GLint prevBuffer = 0;

    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevBuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_handle);
    m_mapped = static_cast<uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(m_size), GL_MAP_READ_BIT));
    glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(prevBuffer));
    m_state = m_mapped ? State::Ready : State::Idle;
    return m_state;
}


const uint8_t* GfxReadTarget::Data(void) {
    return (m_state == State::Ready) ? m_mapped : nullptr;
}


void GfxReadTarget::Release(void) {
    DeleteFence();
    if (m_mapped) {
        GLint prevBuffer = 0;

        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevBuffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_handle);
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(prevBuffer));
        m_mapped = nullptr;
    }
    m_state = State::Idle;
}


void GfxReadTarget::Destroy(void) {
    Release();
    if (m_handle) {
        glDeleteBuffers(1, &m_handle);
        m_handle = 0;
    }
    m_capacity = 0;
    m_size = 0;
}
