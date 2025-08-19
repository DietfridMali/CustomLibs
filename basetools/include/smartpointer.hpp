#pragma once

// =================================================================================================

template<typename DATA_T>
class MemoryHandle {
public:
    DATA_T* m_handle;
    size_t  m_itemCount;
    size_t  m_refCount;
    // Herausforderung: *this und other müssen _denselben_ m_refCount und m_data verwenden
    MemoryHandle()
        : m_handle(nullptr), m_itemCount(0), m_refCount(0)
    {
    }


    MemoryHandle(DATA_T* address)
        : m_handle(address), m_refCount(address ? 1 : 0), m_itemCount(1)
    {
    }


    MemoryHandle(size_t elemCount)
        : m_itemCount(elemCount)
    {
        m_handle = (elemCount > 1) ? new DATA_T[elemCount] : new DATA_T;
        m_refCount = (m_handle ? 1 : 0);
    }


    void Destroy(void) {
        if (m_handle) {
            if (m_itemCount > 1)
                delete[] m_handle;
            else
                delete m_handle;
            m_handle = nullptr;
        }
        m_refCount = 0;
    }


    ~MemoryHandle() {
        Destroy();
    }


    size_t operator++() {
        return ++m_refCount;
    }


    size_t operator--() {
        if (m_refCount > 1)
            return --m_refCount;
        return 0;
    }


    operator bool() {
        return m_handle != nullptr;
    }
};

// =================================================================================================

template<typename DATA_T>
class SmartPointer {
public:
    MemoryHandle<DATA_T>* m_data;

    SmartPointer()
        : m_data(nullptr)
    {
    }


    SmartPointer(size_t elemCount)
        : m_data(new MemoryHandle<DATA_T>(elemCount))
    {
    }


    SmartPointer(DATA_T* data)
        : m_data(new MemoryHandle<DATA_T>(data))
    {
    }


    SmartPointer(const SmartPointer& other)
    {
        if ((m_data = other.m_data))
            ++(*m_data);
    }


    SmartPointer(SmartPointer&& other) {
        Move(other);
    }


    SmartPointer& operator=(const SmartPointer& other) {
        if (other.m_data) {
            m_data = other.m_data;
            ++(*m_data);
        }
        return *this;
    }


    SmartPointer& operator=(SmartPointer&& other) {
        return Move(other);
    }


    SmartPointer& Move(SmartPointer& other)
    {
        if (m_data != other.m_data) {
            if (m_data)
                --(*m_data);
            m_data = other.m_data;
            other.m_data = nullptr;
        }
        return *this;
    }


    void Destroy(void) {
        if (m_data and !--(*m_data))
            delete m_data;
    }


    ~SmartPointer() {
        Destroy();
    }


    DATA_T& operator*() {
        // throw exception if m_data->m_handle == nullptr
        return *m_resource->m_handle;
    }


    DATA_T* operator->() {
        return m_data->m_handle;
    }


    operator DATA_T*() const {
        return m_data ? m_data->m_handle : nullptr;
    }

};

// =================================================================================================
