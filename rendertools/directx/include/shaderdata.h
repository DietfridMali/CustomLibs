#pragma once

#include <utility>
#include <type_traits>
#include <memory>
#include <cstring>
#include "string.hpp"
#include "array.hpp"
#include "vector.hpp"
#include <limits>

// =================================================================================================
// DX12 shaderdata.h
//
// In the OGL version, "location" was a GLint (OpenGL uniform location).
// In DX12, "location" is an int that stores the byte offset of a field inside the b1
// (ShaderConstants) constant buffer.  Everything else (UniformData<T>, UniformArray<T>,
// ShaderLocationTable) is kept structurally identical so that Shader's template helpers
// (GetUniform, UpdateUniform) compile unchanged.

// -------------------------------------------------------------------------------------------------

struct UniformHandle
{
    int    m_location{ std::numeric_limits<int>::min() }; // b1 byte offset; min = not-yet-resolved
    String m_name{ "" };

    UniformHandle() = default;

    UniformHandle(String name, int location = std::numeric_limits<int>::min())
        : m_location{ location }, m_name{ std::move(name) }
    { }

    virtual ~UniformHandle() = default;

    inline int&    Location(void) noexcept { return m_location; }
    inline String& Name(void) noexcept { return m_name; }

    bool operator<(const UniformHandle& o) const noexcept { return m_name < o.m_name; }
    bool operator>(const UniformHandle& o) const noexcept { return m_name > o.m_name; }
    bool operator==(const UniformHandle& o) const noexcept { return m_name == o.m_name; }
    bool operator!=(const UniformHandle& o) const noexcept { return m_name != o.m_name; }
};

// -------------------------------------------------------------------------------------------------

template<typename T>
struct UniformData : public UniformHandle
{
    T m_data{};

    UniformData() = default;
    UniformData(String name, int location, T data = {})
        : UniformHandle(std::move(name), location), m_data(std::move(data)) {}

    UniformData& operator=(const T& other)  { m_data = other; return *this; }
    UniformData& operator=(T&& other) noexcept(std::is_nothrow_move_assignable_v<T>)
        { m_data = std::move(other); return *this; }

    inline bool operator==(const T& other) const noexcept { return m_data == other; }
    inline bool operator!=(const T& other) const noexcept { return !(*this == other); }
    inline T& data(void) noexcept { return m_data; }
};

// -------------------------------------------------------------------------------------------------

template<typename DATA_T>
struct UniformArray : public UniformHandle
{
    std::unique_ptr<DATA_T[]> m_data;
    size_t m_size{ 0 };
    size_t m_length{ 0 };

    UniformArray() = default;

    bool Copy(const DATA_T* data, size_t length) {
        if (not data || length == 0) { m_data.reset(); m_size = m_length = 0; return true; }
        if (not m_data || m_length != length) {
            m_length = length;
            m_size   = m_length * sizeof(DATA_T);
            m_data   = std::make_unique<DATA_T[]>(m_length);
        }
        std::memcpy(m_data.get(), data, m_size);
        return true;
    }

    UniformArray(String name, int location, DATA_T* data = nullptr, size_t length = 0)
        : UniformHandle(std::move(name), location) { Copy(data, length); }

    bool operator()(const DATA_T* data, size_t length) { return Copy(data, length); }

    UniformArray& operator=(const DATA_T* data) { Copy(data, m_length); return *this; }

    bool operator==(const DATA_T* other) const noexcept {
        return m_data && other && std::memcmp(m_data.get(), other, m_size) == 0;
    }
    bool operator!=(const DATA_T* other) const noexcept { return !(*this == other); }

    DATA_T*       Data() noexcept       { return m_data.get(); }
    const DATA_T* Data()   const noexcept { return m_data.get(); }
    size_t        Length() const noexcept { return m_length; }
    size_t        Size()   const noexcept { return m_size; }
};

// -------------------------------------------------------------------------------------------------

template<typename DATA_T, size_t ElemCount>
struct FixedUniformArray : public UniformArray<DATA_T>
{
    using Base = UniformArray<DATA_T>;

    FixedUniformArray() = default;
    FixedUniformArray(String name, int location) : Base(std::move(name), location) {}

    using Base::operator=;

    inline bool Copy(const DATA_T* data)          { return Base::Copy(data, ElemCount); }
    inline bool operator()(const DATA_T* data)    { return Base::Copy(data, ElemCount); }

    FixedUniformArray& operator=(const DATA_T*&& data) { Base::Copy(data, ElemCount); return *this; }
};

// -------------------------------------------------------------------------------------------------
// ShaderLocationTable — maps uniform names to their b1 byte offsets.
// On first access the offset is std::numeric_limits<int>::min() (unknown).
// Shader::SetB1Field() resolves it via HLSL reflection and stores it here.

class ShaderLocationTable
{
public:
    struct ShaderLocation {
        String m_name{ "" };
        int    m_nameLength{ 0 };
        int    m_location{ std::numeric_limits<int>::min() };

        ShaderLocation(String name = "")
            : m_name(name), m_nameLength(name.Length()),
              m_location(std::numeric_limits<int>::min())
        { }

        inline String& Name() noexcept { return m_name; }
        inline int*    Location() noexcept { return &m_location; }

        inline bool operator==(const String& name) const {
            return (m_nameLength == name.Length()) && (m_name == name);
        }
    };

private:
    AutoArray<ShaderLocation> m_locations;

public:
    ShaderLocationTable() = default;
    ~ShaderLocationTable() = default;

    void Start(void) { /* nothing */ }

    int* operator[](const String name) noexcept {
        for (auto& loc : m_locations)
            if (loc == name) return loc.Location();
        ShaderLocation* loc;
        try { loc = m_locations.Append(); }
        catch (...) { return nullptr; }
        if (not loc) return nullptr;
        *loc = ShaderLocation(name);
        return loc->Location();
    }
};

// -------------------------------------------------------------------------------------------------

using UniformVector2f = UniformData<Vector2f>;
using UniformVector3f = UniformData<Vector3f>;
using UniformVector4f = UniformData<Vector4f>;
using UniformFloat    = UniformData<float>;
using UniformInt      = UniformData<int>;
using UniformArray9f  = FixedUniformArray<float, 9>;
using UniformArray16f = FixedUniformArray<float, 16>;
using UniformArray2i  = FixedUniformArray<int, 2>;
using UniformArray3i  = FixedUniformArray<int, 3>;
using UniformArray4i  = FixedUniformArray<int, 4>;

// =================================================================================================
