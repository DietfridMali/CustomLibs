#pragma once

#include <memory>
#include <functional>
#include <cstddef> // für std::size_t

// Für OpenGL-Typen und Funktionen
#include <GL/gl.h> // oder <GL/glew.h> oder <GL/glcorearb.h> je nach System/Projekt

// =================================================================================================

template <typename HANDLE_T>
class SharedHandle {
protected:
    struct HandleInfo {
        HANDLE_T                           handle;
        std::function<void(HANDLE_T)>      releaser;

        HandleInfo(HANDLE_T h, std::function<void(HANDLE_T)> r)
            : handle(h), releaser(std::move(r))
        {
        }
    };

    std::shared_ptr<HandleInfo>            m_info;
    std::function<void(HANDLE_T)>          m_releaser;
    std::function<HANDLE_T()>              m_allocator;

private:
    static void Dispose(HandleInfo* p) noexcept {
        if (p) {
            try {
                if (p->releaser && p->handle)
                    p->releaser(p->handle);
            }
            catch (...) { }
            delete p;
        }
    }


public:
    SharedHandle() = default;

    SharedHandle(HANDLE_T handle, std::function<HANDLE_T()> allocator, std::function<void(HANDLE_T)> releaser)
        : m_releaser(std::move(releaser))
        , m_allocator(std::move(allocator))
        , m_info(
            handle
            ? std::shared_ptr<HandleInfo>(new HandleInfo(handle, m_releaser), &Dispose)
            : std::shared_ptr<HandleInfo>()
        )
    {
    }

    // Copy/Move-Constructor und Assignment
    SharedHandle(const SharedHandle&) = default;

    SharedHandle(SharedHandle&&) noexcept = default;

    SharedHandle& operator=(const SharedHandle&) = default;

    SharedHandle& operator=(SharedHandle&&) noexcept = default;

    SharedHandle& operator=(HANDLE_T h) noexcept {
        if (h == HANDLE_T{}) { 
            Release(); 
            return *this; 
        }
        m_info = std::shared_ptr<HandleInfo>(new HandleInfo(h, m_releaser), &Dispose);
        return *this;
    }


    SharedHandle& operator=(std::nullptr_t) noexcept { 
        Release(); 
        return *this; 
    }


    // Gleichheitsoperator
    inline bool operator==(const SharedHandle& other) const
        noexcept
    {
        if (!m_info and !other.m_info)
            return true;
        if (!m_info or !other.m_info)
            return false;
        return m_info->handle == other.m_info->handle;
    }

    inline bool operator!=(const SharedHandle& other) const
        noexcept
    {
        return not operator==(other);
    }

    bool operator==(const HANDLE_T handle) const
        noexcept
    {
        return m_info ? m_info->handle == handle : false;
    }

    inline HANDLE_T Data() const
        noexcept
    {
        return m_info ? m_info->handle : HANDLE_T{};
    }

    inline operator HANDLE_T() const
        noexcept
    {
        return Data();
    }

    inline bool IsAvailable(void) const
        noexcept
    {
        return Data() != HANDLE_T{};
    }

    inline operator bool() const
        noexcept
    {
        return this->IsAvailable();
    }

    inline bool operator!() const
        noexcept
    {
        return not this->IsAvailable();
    }

    inline void Release()
        noexcept
    {
        if (m_info and m_info->handle and m_releaser)
            m_info.reset(); // custom deleter ruft releaser automatisch
    }

    // Claim: gibt alten frei, legt neuen an (wenn allocator gesetzt)
    HANDLE_T Claim()
        noexcept
    {
        Release();
        if (!m_allocator)
            return HANDLE_T{};
        HANDLE_T newHandle = m_allocator();
        if (newHandle) {
            m_info = std::shared_ptr<HandleInfo>(new HandleInfo(newHandle, m_releaser), &Dispose);
        }
        return newHandle;
    }

    inline std::size_t RefCount() const
        noexcept
    {
        return m_info ? m_info.use_count() : 0;
    }
};

// =================================================================================================

using glBufferAllocator = void (*)(GLsizei, GLuint*);
using glBufferReleaser = void (*)(GLsizei, const GLuint*);

class SharedGLHandle 
    : public SharedHandle<GLuint> 
{
public:
    SharedGLHandle() = default;

    SharedGLHandle(GLuint handle, glBufferAllocator allocator, glBufferReleaser releaser)
        : SharedHandle(
            handle,
            [allocator]() { GLuint h; if (allocator == nullptr) h = 0; else allocator(1, &h); return h; },
            [releaser](GLuint h) { if (h and (releaser != nullptr)) releaser(1, &h); }
        )
    {
    }
};


class SharedTextureHandle : public SharedGLHandle {
public:
    SharedTextureHandle(GLuint handle = 0)
        : SharedGLHandle(handle, glGenTextures, glDeleteTextures)
    {
    }
};


class SharedBufferHandle : public SharedGLHandle {
public:
    SharedBufferHandle(GLuint handle = 0)
        : SharedGLHandle(handle, glGenBuffers, glDeleteBuffers)
    {
    }
};


class SharedFramebufferHandle : public SharedGLHandle {
public:
    SharedFramebufferHandle(GLuint handle = 0)
        : SharedGLHandle(handle, glGenFramebuffers, glDeleteFramebuffers)
    {
    }
};

// =================================================================================================
