#pragma once

#include <memory>
#include <variant>
#include <cstddef>
#include <stdexcept>

// =================================================================================================

template <typename DATA_T>
class SharedPointer {
    std::variant<std::shared_ptr<DATA_T>, std::shared_ptr<DATA_T[]>> m_ptr;

public:
    bool m_isArray = false;

    SharedPointer() = default;

    explicit SharedPointer(std::size_t count) { Claim(count); }

    SharedPointer(const SharedPointer& other) noexcept
        : m_ptr(other.m_ptr), m_isArray(other.m_isArray) {
    }

    SharedPointer(SharedPointer&& other) noexcept
        : m_ptr(std::move(other.m_ptr)), m_isArray(other.m_isArray) {
        other.m_isArray = false;
    }

    SharedPointer& operator=(const SharedPointer& other) noexcept {
        if (this != &other) {
            m_ptr = other.m_ptr;
            m_isArray = other.m_isArray;
        }
        return *this;
    }

    SharedPointer& operator=(SharedPointer&& other) noexcept {
        if (this != &other) {
            m_ptr = std::move(other.m_ptr);
            m_isArray = other.m_isArray;
            other.m_isArray = false;
        }
        return *this;
    }

    DATA_T* Claim(std::size_t count = 1) {
        if (count > 1) {
            m_ptr = std::shared_ptr<DATA_T[]>(new DATA_T[count]());
            m_isArray = true;
            return std::get<std::shared_ptr<DATA_T[]>>(m_ptr).get();
        }
        else if (count == 1) {
            m_ptr = std::make_shared<DATA_T>();
            m_isArray = false;
            return std::get<std::shared_ptr<DATA_T>>(m_ptr).get();
        }
        m_ptr = {};
        m_isArray = false;
        return nullptr;
    }

    void Release() noexcept {
        m_ptr = {};
        m_isArray = false;
    }

    DATA_T* get() noexcept {
        if (m_isArray) {
            if (auto p = std::get_if<std::shared_ptr<DATA_T[]>>(&m_ptr)) return p->get();
            return nullptr;
        }
        else {
            if (auto p = std::get_if<std::shared_ptr<DATA_T>>(&m_ptr)) return p->get();
            return nullptr;
        }
    }

    const DATA_T* get() const noexcept {
        if (m_isArray) {
            if (auto p = std::get_if<std::shared_ptr<DATA_T[]>>(&m_ptr)) return p->get();
            return nullptr;
        }
        else {
            if (auto p = std::get_if<std::shared_ptr<DATA_T>>(&m_ptr)) return p->get();
            return nullptr;
        }
    }

    DATA_T& operator[](std::size_t i) {
        if (!m_isArray) throw std::logic_error("Kein Array verwaltet!");
        return std::get<std::shared_ptr<DATA_T[]>>(m_ptr).get()[i];
    }

    const DATA_T& operator[](std::size_t i) const {
        if (!m_isArray) throw std::logic_error("Kein Array verwaltet!");
        return std::get<std::shared_ptr<DATA_T[]>>(m_ptr).get()[i];
    }

    DATA_T& operator*() {
        if (m_isArray) throw std::logic_error("Array-Modus: Kein Einzelobjekt dereferenzierbar!");
        DATA_T* ptr = get();
        if (!ptr) throw std::runtime_error("Nullpointer!");
        return *ptr;
    }

    DATA_T* operator->() {
        if (m_isArray) throw std::logic_error("Array-Modus: Kein Einzelobjekt dereferenzierbar!");
        return get();
    }

    inline bool IsValid(void) const noexcept { return get() != nullptr; }

    operator bool() const noexcept { return IsValid(); }

    bool operator!() const noexcept { return not IsValid(); }

    operator DATA_T* () noexcept { return static_cast<DATA_T*>(get()); }

    operator const DATA_T* () const noexcept { return static_cast<const DATA_T*>(get()); }

    operator void* () noexcept { return static_cast<void*>(get()); }

    operator const void* () const noexcept { return static_cast<const void*>(get()); }

    bool isArray() const noexcept { return m_isArray; }

    // ---- nur noexcept ergänzt (kein Umbau) ----

    bool operator==(const SharedPointer& other) const
        noexcept
    {
        if (!m_isArray && !other.m_isArray)
            return get() == other.get();
        if (m_isArray != other.m_isArray)
            return false;
        return get() == other.get();
    }

    inline bool operator!=(const SharedPointer& other) const
        noexcept
    {
        return !(*this == other);
    }
};

// =================================================================================================
