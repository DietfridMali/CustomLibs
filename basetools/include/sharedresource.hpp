#pragma once

#if USE_STD

#   error custom shared handle included

#else

// =================================================================================================

template<typename DATA_T>
class SharedHandle {
public:
    DATA_T  m_handle;
    size_t  m_refCount;
    bool    m_isStatic;

    SharedHandle(DATA_T handle, DATA_T nullValue, bool isStatic = false)
        : m_handle(handle), m_refCount(1), m_isStatic(isStatic)
    { }


    virtual DATA_T Claim(size_t capacity = 1) = 0;


    virtual void Release(void) = 0;


    virtual operator bool() = 0;


    inline bool IsStatic(void) const {
        return m_isStatic;
    }


    inline size_t RefCount(void) const {
        return m_refCount;
    }


    size_t operator++() {
        return ++m_refCount;
    }


    size_t operator--() {
        if (m_refCount > 0)
            return --m_refCount;
        return 0;
    }


    ~SharedHandle() {
        //Release();
    }
};

// =================================================================================================

template<typename DATA_T>
class SharedResourceHandler {
public:
    SharedHandle<DATA_T>* m_resource;


    SharedResourceHandler(SharedHandle<DATA_T>* resource = nullptr)
        : m_resource(resource)
    { }


    void Release(void) {
        if (m_resource) {
            m_resource->Release();
            if (not m_resource->m_refCount)
                delete m_resource;
        }
        m_resource = nullptr;
    }


    SharedResourceHandler(const SharedResourceHandler& other) {
        Copy(other);
    }


    SharedResourceHandler(SharedResourceHandler&& other) {
        Move(other);
    }


    ~SharedResourceHandler() {
        Release();
        delete m_resource;
        m_resource = nullptr;
    }


    SharedResourceHandler& operator=(const SharedResourceHandler& other) {
        return Copy(other);
    }


    SharedResourceHandler& operator=(SharedResourceHandler&& other) noexcept {
        return Move(other);
    }


    bool operator== (const SharedResourceHandler& other) const {
        return m_resource == other.m_resource;
    }


    bool operator!= (const SharedResourceHandler& other) const {
        return m_resource != other.m_resource;
    }


    SharedResourceHandler& Copy(const SharedResourceHandler& other) {
        if (m_resource != other.m_resource) {
            if (m_resource)
                m_resource->Release();
            m_resource = other.m_resource;
            if (m_resource)
                ++(*m_resource);
        }
        return *this;
    }


    SharedResourceHandler& Move(SharedResourceHandler& other) {
        if (m_resource != other.m_resource) {
            if (m_resource)
                m_resource->Release();
            m_resource = other.m_resource;
            other.m_resource = nullptr;
        }
        return *this;
    }


    inline DATA_T Handle(void) {
        return m_resource ? m_resource->m_handle : (DATA_T) 0;
    }


    inline size_t RefCount(void) {
        return m_resource ? !m_resource->RefCount() : 0;
    }


    inline bool IsStatic(void) const {
        return !m_resource or m_resource->IsStatic();
    }


    size_t operator++() {
        if (m_resource)
            return ++(*m_resource);
        return 0;
    }


    size_t operator--() {
        if (m_resource)
            return --(*m_resource);
        return 0;
    }


    operator bool() {
        return (m_resource != nullptr) and (DATA_T*)m_resource;
    }
};

// =================================================================================================

#endif
