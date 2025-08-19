#pragma once

#include <memory>
#include <variant>
#include <cstddef>
#include <stdexcept>

template <typename DATA_T>
class SharedPointer {
    std::variant<std::shared_ptr<DATA_T>, std::shared_ptr<DATA_T[]>> m_ptr;

public:
    bool m_isArray = false;

    // Default
    SharedPointer() = default;

    // Heapobjekt/Array direkt anlegen
    explicit SharedPointer(std::size_t count) { Claim(count); }

    // Copy-Konstruktor
    SharedPointer(const SharedPointer& other)
        : m_ptr(other.m_ptr), m_isArray(other.m_isArray) {
    }

    // Move-Konstruktor
    SharedPointer(SharedPointer&& other) noexcept
        : m_ptr(std::move(other.m_ptr)), m_isArray(other.m_isArray) {
        other.m_isArray = false;
    }

    // Copy-Assignment
    SharedPointer& operator=(const SharedPointer& other) {
        if (this != &other) {
            m_ptr = other.m_ptr;
            m_isArray = other.m_isArray;
        }
        return *this;
    }

    // Move-Assignment
    SharedPointer& operator=(SharedPointer&& other) noexcept {
        if (this != &other) {
            m_ptr = std::move(other.m_ptr);
            m_isArray = other.m_isArray;
            other.m_isArray = false;
        }
        return *this;
    }

    // Allokation/Reset
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

    void Release() {
        m_ptr = {};
        m_isArray = false;
    }

    // Zugriff
    DATA_T* get() {
        if (m_isArray) {
            return std::get<std::shared_ptr<DATA_T[]>>(m_ptr).get();
        }
        else {
            return std::get<std::shared_ptr<DATA_T>>(m_ptr).get();
        }
    }
    const DATA_T* get() const {
        if (m_isArray) {
            return std::get<std::shared_ptr<DATA_T[]>>(m_ptr).get();
        }
        else {
            return std::get<std::shared_ptr<DATA_T>>(m_ptr).get();
        }
    }

    // Array-Zugriff
    DATA_T& operator[](std::size_t i) {
        if (!m_isArray) throw std::logic_error("Kein Array verwaltet!");
        return std::get<std::shared_ptr<DATA_T[]>>(m_ptr)[i];
    }
    const DATA_T& operator[](std::size_t i) const {
        if (!m_isArray) throw std::logic_error("Kein Array verwaltet!");
        return std::get<std::shared_ptr<DATA_T[]>>(m_ptr)[i];
    }

    // Einzelobjekt-Zugriff
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

    inline bool IsValid(void) const { return get() != nullptr; }
    // Bool-Test
    operator bool() const { return IsValid(); }

    bool operator!() const { return not IsValid(); }

    // Für C-APIs:
    operator DATA_T*() { return static_cast<DATA_T*>(get()); }

    operator const DATA_T*() const { return static_cast<const DATA_T*>(get()); }

    operator void*() { return static_cast<void*>(get()); }

    operator const void*() const { return static_cast<const void*>(get()); }

    // Abfrage Array/Objekt
    bool isArray() const { return m_isArray; }
};
