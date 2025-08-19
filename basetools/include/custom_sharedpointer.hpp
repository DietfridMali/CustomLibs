#pragma once

#if USE_STD

#   error custom shared pointer included

#else


#define NOMINMAX

#include <algorithm>
#include <type_traits>

#include "sharedresource.hpp"

// =================================================================================================

template<typename DATA_T>
class SharedMemoryHandle
    : public SharedHandle<DATA_T*>
{
public:
    //using DATA_T = typename std::remove_pointer<DATA_T>::type;

    bool    m_isArray;


    SharedMemoryHandle()
        : SharedHandle(nullptr, nullptr), m_isArray(false)
    {
    }


    SharedMemoryHandle(DATA_T* handle, bool isArray = false, bool isStatic = false)
        : SharedHandle<DATA_T*>(handle, nullptr, isStatic), m_isArray(isArray)
    {
    }


    virtual DATA_T* Claim(size_t itemCount) {
        Release();
        if (itemCount) {
            if (itemCount > 1)
                this->m_isArray = true;
            this->m_handle = this->m_isArray ? new DATA_T[itemCount] : new DATA_T;
            this->m_refCount = (this->m_handle ? 1 : 0);
            this->m_isStatic = false;
        }
        return this->m_handle;
    }


    virtual void Release(void) {
        if (this->m_refCount > 1)
            --(this->m_refCount);
        else if (this->m_handle) {
            if (this->m_isStatic) 
                this->m_isStatic = false;
            else {
                if (this->m_isArray)
                    delete[] this->m_handle;
                else
                    delete this->m_handle;
            }
            this->m_refCount = 0;
            this->m_handle = nullptr;
        }
    }


    virtual operator bool() {
        return this->m_handle != nullptr;
    }
};

// =================================================================================================

template<typename DATA_T>
class SharedPointer
    : public SharedResourceHandler<DATA_T*>
{
public:
    struct QualifiedPointer {
        DATA_T* dataPtr = nullptr;
        bool    isArray = false;
        bool    isStatic = false;
    };

    SharedPointer()
    { }


    SharedPointer(DATA_T* dataPtr, bool isArray = false, bool isStatic = false)
        : SharedResourceHandler<DATA_T*>(new SharedMemoryHandle<DATA_T>(dataPtr, isArray, isStatic))
    { }



    DATA_T* Claim(size_t capacity = 1) {
        this->m_resource->Release();
        this->m_resource = new SharedMemoryHandle<DATA_T>(nullptr);
        return this->m_resource ? this->m_resource->Claim(capacity) : nullptr;
    }


    SharedPointer& operator=(const DATA_T* dataPtr) {
        if (dataPtr == nullptr) {
            this->Release();  // ruft korrekt Release() und setzt m_resource = nullptr
        }
        else {
            this->m_resource = new SharedMemoryHandle<DATA_T>(const_cast<DATA_T*>(dataPtr), false, false);
        }
        return *this;
    }


    SharedPointer& operator=(const QualifiedPointer& qualifiedPtr) {
        if (qualifiedPtr.dataPtr == nullptr) {
            this->Release();  // ruft korrekt Release() und setzt m_resource = nullptr
        }
        else {
            this->m_resource = new SharedMemoryHandle<DATA_T>(qualifiedPtr.dataPtr, qualifiedPtr.isArray, qualifiedPtr.isStatic);
        }
        return *this;
    }


    DATA_T& operator*() {
        // throw exception if m_data->m_address == nullptr
        return *(this->m_resource->m_handle);
    }


    DATA_T* operator->() {
        return this->m_resource->m_handle;
    }


    operator DATA_T*() const {
        return this->m_resource ? this->m_resource->m_handle : nullptr;
    }


    inline bool IsAvailable(void) const {
        return this->m_resource and this->m_resource->m_handle;
    }
};

// =================================================================================================

#endif
