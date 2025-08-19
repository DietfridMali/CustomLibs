#pragma once

#if USE_STD

#   error custom shared GL handle included

#else


#include "sharedresource.hpp"

// =================================================================================================

using glBufferAllocator = void (*)(GLsizei, GLuint*);
using glBufferReleaser = void (*)(GLsizei, const GLuint*);

class SharedGLBufferHandle
    : public SharedHandle<GLuint>
{
public:
    glBufferAllocator   m_allocate;
    glBufferReleaser    m_release;

#if 0
    SharedGLBufferHandle(glBufferAllocator allocate, glBufferReleaser release)
        : m_allocate(allocate), m_release(release), SharedHandle<GLuint>(0, 0)
    {
        Claim();
    }
#endif

    SharedGLBufferHandle(GLuint handle, glBufferAllocator allocate, glBufferReleaser release)
        : SharedHandle<GLuint>(handle, (GLuint) 0), m_allocate(allocate), m_release(release)
    {
    }


    virtual GLuint Claim(size_t capacity = 1) {
        this->m_allocate(1, &this->m_handle);
        this->m_refCount = 1;
        return this->m_handle;
    }


    virtual void Release(void) {
        if (this->m_refCount > 1)
            --(this->m_refCount);
        else if (this->m_handle) {
            this->m_release(1, &this->m_handle);
            this->m_handle = 0;
            this->m_refCount = 0;
        }
    }


    virtual operator bool() {
        return this->m_handle != 0;
    }
};

// =================================================================================================

class SharedGLHandle
    : public SharedResourceHandler<GLuint>
{
public:
    using Base = SharedResourceHandler<GLuint>;


    SharedGLHandle()
        : Base(nullptr)
    {
    }


    SharedGLHandle(GLuint handle, glBufferAllocator allocate, glBufferReleaser release)
        : Base(new SharedGLBufferHandle(handle, allocate, release))
    {
    }


    GLuint Claim(size_t capacity = 1) {
        if (not m_resource)
            return 0;
        glBufferAllocator allocate = static_cast<SharedGLBufferHandle*>(m_resource)->m_allocate;
        glBufferReleaser release = static_cast<SharedGLBufferHandle*>(m_resource)->m_release;
        m_resource->Release();
        m_resource = new SharedGLBufferHandle(0, allocate, release);
        return m_resource ? m_resource->Claim(capacity) : (GLuint)0;
    }


    GLuint operator*() {
        // throw exception if m_data->m_address == nullptr
        return m_resource ? m_resource->m_handle : 0;
    }


    operator GLuint() const {
        return m_resource ? m_resource->m_handle : 0;
    }


    inline bool IsAvailable(void) {
        return m_resource and m_resource->m_handle;
    }
};

// =================================================================================================

class SharedTextureHandle
    : public SharedGLHandle
{
public:
    SharedTextureHandle(GLuint handle = 0)
        : SharedGLHandle(handle, glGenTextures, glDeleteTextures)
    { }
};

// =================================================================================================

class SharedBufferHandle
    : public SharedGLHandle
{
public:
    SharedBufferHandle(GLuint handle = 0)
        : SharedGLHandle(handle, glGenBuffers, glDeleteBuffers)
    { }
};

// =================================================================================================

class SharedFramebufferHandle
    : public SharedGLHandle
{
public:
    SharedFramebufferHandle(GLuint handle = 0)
        : SharedGLHandle(handle, glGenFramebuffers, glDeleteFramebuffers)
    {
    }
};

// =================================================================================================

#endif
