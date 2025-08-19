#pragma once

#include <utility>
#include <type_traits>
#include "string.hpp"
#include <limits>

// =================================================================================================
// The following code is meant to make caching of uniform variable locations and data as easy as possible.
// Each shader will hold a list (ManagedArray / std::vector) of uniform variable class instances as 
// defined below.

struct UniformHandle
{
    GLint   m_location{};
    String  m_name{ "" };

    UniformHandle() = default;
    UniformHandle(String name, GLint location)
        : m_location{ location }, m_name{ std::move(name) }
    {
    }

    virtual ~UniformHandle() = default; // falls du polymorph speichern willst

    inline GLint& Location(void) { return m_location; }

    inline String& Name(void) { return m_name; }

    bool operator<(const UniformHandle& other) const noexcept { return m_name < other.m_name; }

    bool operator>(const UniformHandle& other) const noexcept { return m_name > other.m_name; }

    bool operator==(const UniformHandle& other) const noexcept { return m_name == other.m_name; }

    bool operator!=(const UniformHandle& other) const noexcept { return m_name != other.m_name; }
};

// -------------------------------------------------------------------------------------------------

template<typename T>
struct UniformData
    : public UniformHandle
{
    T m_data{};

    UniformData() = default;
    UniformData(String name, GLint location, T data = {})
        : UniformHandle(std::move(name), location), m_data(std::move(data)) {
    }

    // Zuweisung AUS einem T
    UniformData& operator=(const T& other) {
        m_data = other;
        return *this;
    }
    UniformData& operator=(T&& other) noexcept(std::is_nothrow_move_assignable_v<T>) {
        m_data = std::move(other);
        return *this;
    }

    // optional: Vergleich mit T
    inline bool operator==(const T& other) const noexcept { return m_data == other; }
    inline bool operator!=(const T& other) const noexcept { return !(*this == other); }

    inline T& data(void) { return m_data; }
};

// -------------------------------------------------------------------------------------------------

template<typename DATA_T>
struct UniformArrayData {
    DATA_T* m_data;
    size_t  m_length;

    void operator()(DATA_T* data, size_t length) {
        return Copy(data, length);
    }
};

template<typename DATA_T>
struct UniformArray : public UniformHandle {
    std::unique_ptr<DATA_T[]>   m_data; // eigener Speicher
    size_t                      m_size{ 0 }; // Größe in BYTES
    size_t                      m_length{ 0 };

    UniformArray() = default;

    bool Copy(const DATA_T* data, size_t length) {
        if (not data or (length == 0)) {
            m_data.reset();
            m_size =
                m_length = 0;
            return true;
        }
        if (not m_data or (m_length != length)) {
            m_length = length;
            m_size = m_length * sizeof(DATA_T);
            m_data = std::make_unique<DATA_T[]>(m_length);
        }
        std::memcpy(m_data.get(), data, m_size);
        return true;
    }

    UniformArray(String name, GLint location, DATA_T* data = nullptr, size_t length = 0)
        : UniformHandle(std::move(name), location)
    {
        Copy(data, length);
    }

    // Setzen/Updaten via Funktionsaufruf-Operator:
    // - Wenn Größe unterschiedlich: reallozieren + kopieren -> true zurück
    // - Wenn Größe gleich: memcmp, falls anders -> kopieren -> true, sonst false
    bool operator()(const DATA_T* data, size_t length) {
        return Copy(data, length);
    }


    UniformArray& operator=(const DATA_T* data) {
        Copy(data, m_length);
        return *this;
    }


    bool operator==(const DATA_T* other) const noexcept {
        return m_data and other and std::memcmp(m_data.get(), other, m_size) == 0;
    }


    bool operator!=(const DATA_T* other) const noexcept {
        return not (*this == other); // Komma-Operator als Guard gegen versehentliche Klammerfehler
    }


    // Vergleich mit externem Buffer + Bytegröße
    bool operator==(UniformArrayData<DATA_T*>&& other) const noexcept {
        if (not m_data or not other.m_data)
            return false;
        if (m_length != other.m_length) // also catches size == 0
            return false;
        return std::memcmp(m_data.get(), other.m_data, m_size) == 0;
    }


    bool operator!=(UniformArrayData<DATA_T*>&& other) const noexcept {
        return !(*this == other); // Komma-Operator als Guard gegen versehentliche Klammerfehler
    }

    // Zugriff
    inline DATA_T* Data() noexcept { return m_data.get(); }

    inline const DATA_T* Data() const noexcept { return m_data.get(); }

    // Größen-API
    static constexpr size_t Length() noexcept { return m_length; }

    static constexpr size_t Size()    noexcept { return m_size; }
};

// -------------------------------------------------------------------------------------------------

template<typename DATA_T, size_t ElemCount>
struct FixedUniformArray 
    : public UniformArray<DATA_T> 
{
    using Base = UniformArray<DATA_T>;

    FixedUniformArray() = default;

    FixedUniformArray(String name, GLint location)
        : Base(std::move(name), location) {
    }

    using Base::operator=;

    // fester Copy/Update ohne size-Parameter
    inline bool Copy(const DATA_T* data) {
        return Base::Copy(data, ElemCount);
    }

    inline bool operator()(const DATA_T* data) {
        return Base::Copy(data, ElemCount);
    }

    FixedUniformArray& operator=(const DATA_T*&& data) {
        Base::Copy(data, ElemCount);
        return *this;
    }

#if 0
    // Vergleich gegen externen Buffer gleicher fester Größe
    inline bool operator==(const DATA_T* other) const noexcept {
        return Base::operator==(other, ElemCount);
    }

    inline bool operator!=(const DATA_T* other) const noexcept {
        return !(*this == other);
    }
#endif
};

// -------------------------------------------------------------------------------------------------
// ShaderLocationTable's use is intended as follows: For each shader instance, a ShaderLocationTable
// must be provided. ShaderLocationTable entries are sequentially passed to uniform setter calls as
// follows:
// void LoadShader(String shaderName) {
// Shader* shader = shaderHandler.Setup(shaderName);
// if (shader) {
//     static ShaderLocationTable locations;
//     locations.Start();
//     shader->SetFloat("someFloatUniform", locations.Current(), 1.0f);
//     shader->SetVector4f("someVectorUniform", locations.Current(), Vector4f(1,1,1,1));
//     etc.
// 
// The way this works is that the first time a locations entry is referenced it is initialized with 
// a value OpenGL will not use or return. To the shader's uniform management this means that the 
// location hasn't been retrieved yet. In that case, the uniform location will be retrieved using
// glGetUniformLocation and be stored in the location entry passed to the shader's uniform setter.
// If the uniform location is valid (>= 0), a new uniform variable cache instance will be created
// and be used for further writing to that location, only updating it when its contents changes.
// The entire optimization brings a speedup of almost 50% just in debug mode.
// 
// You need to regard that location's entries are tied to uniform variables strictly by the
// call sequence here. You can ofc. use defines or constexprs to define aptly named index values.
// You also need to be aware that making locations static means there is only a single table instance
// for all instances of the method it has been declared in. This is not an issue as it will always
// be used the same way, but you need to be cautious if a method is called on different shaders
// which share uniform variable names. In that case, that method needs a ShaderLocationTable for each
// shader it writes to.

#if 1 // simple version

class ShaderLocationTable {
public:
private:
    ManagedArray<GLint> m_locations;
    int                 m_index{ -1 };

public:
    ShaderLocationTable()
        : m_index(-1)
    {
        m_locations.SetAutoFit(true);
        m_locations.SetShrinkable(false);
        m_locations.SetDefaultValue(std::numeric_limits<GLint>::min());
    }

    ~ShaderLocationTable() = default;

    GLint& operator[](int32_t i) { return m_locations[i]; }

    void Start(void) { m_index = -1; }

    GLint& Current(void) { return m_locations[++m_index]; }

};

#else

class ShaderLocationTable {
public:
    struct ShaderLocation {
        const char* m_name{ "" };
        GLint       m_location{ std::numeric_limits<GLint>::min() };

        ShaderLocation(const char* name = "") 
            : m_name(name), m_location(std::numeric_limits<GLint>::min())
        { }
    };

    struct LocationIndex {
        int32_t     i;
        const char* name;
    };

private:
    ManagedArray<ShaderLocation> m_locations;

public:
    ShaderLocationTable() = default;
    ~ShaderLocationTable() = default;

    ShaderLocation& operator[](LocationIndex i) {
        if (i.i >= m_locations.Length()) {
            m_locations.Resize(i.i + 1);
            m_locations [i.i] = ShaderLocation(i.name);
        }
        return m_locations[i.i];
    }
};

#endif

// -------------------------------------------------------------------------------------------------

using UniformVector4f = UniformData<Vector4f>;

using UniformVector3f = UniformData<Vector3f>;

using UniformVector2f = UniformData<Vector2f>;

using UniformFloat = UniformData<float>;

using UniformInt = UniformData<int>;

using UniformArray9f = FixedUniformArray<float, 9>;

using UniformArray16f = FixedUniformArray<float, 16>;

using UniformArray2i = FixedUniformArray<int, 2>;

using UniformArray3i = FixedUniformArray<int, 3>;

using UniformArray4i = FixedUniformArray<int, 4>;

// =================================================================================================

