#pragma once

#include <type_traits>
#include <functional>
#include <cstring>
#include <memory>

#include "glew.h"
#include "array.hpp"
#include "dictionary.hpp"
#include "vector.hpp"
#include "string.hpp"
#include "texture.h"
#include "shaderdata.h"

#define CACHE_SHADER_DATA 1

namespace UniformFuncs {
    template<typename S, int C> struct gl_uniform;               // Primary

    // float
    template<> struct gl_uniform<float, 1> { using fn_t = PFNGLUNIFORM1FVPROC; static inline fn_t& fn = glUniform1fv; };
    template<> struct gl_uniform<float, 2> { using fn_t = PFNGLUNIFORM2FVPROC; static inline fn_t& fn = glUniform2fv; };
    template<> struct gl_uniform<float, 3> { using fn_t = PFNGLUNIFORM3FVPROC; static inline fn_t& fn = glUniform3fv; };
    template<> struct gl_uniform<float, 4> { using fn_t = PFNGLUNIFORM4FVPROC; static inline fn_t& fn = glUniform4fv; };

    // int
    template<> struct gl_uniform<int, 1> { using fn_t = PFNGLUNIFORM1IVPROC; static inline fn_t& fn = glUniform1iv; };
    template<> struct gl_uniform<int, 2> { using fn_t = PFNGLUNIFORM2IVPROC; static inline fn_t& fn = glUniform2iv; };
    template<> struct gl_uniform<int, 3> { using fn_t = PFNGLUNIFORM3IVPROC; static inline fn_t& fn = glUniform3iv; };
    template<> struct gl_uniform<int, 4> { using fn_t = PFNGLUNIFORM4IVPROC; static inline fn_t& fn = glUniform4iv; };
} 

// =================================================================================================

template <typename T, typename = void>
struct ScalarTraits {
    using scalarType = std::remove_cv_t<T>;
    static constexpr int componentCount = 1;
};
template <typename T>
struct ScalarTraits<T, std::void_t<typename T::value_type>> {
    using scalarType = std::remove_cv_t<typename T::value_type>;
    static constexpr int componentCount = int(sizeof(T) / sizeof(typename T::value_type));
};
template <typename T>
using ScalarBaseType = typename ScalarTraits<std::remove_cv_t<T>>::scalarType;

template <typename T>
inline constexpr int ComponentCount = ScalarTraits<std::remove_cv_t<T>>::componentCount;

// =================================================================================================
// Some basic shader handling: Compiling, enabling, setting shader variables
// Shaders optimize shader location retrieval and uniform value updates by caching these values;
// see comments in shaderdata.h
// Some remarks about optimization:
// #1 Storing all uniform caches in a global map for all shaders actually slowed the renderer down
// significantly (by about 20%)
// Storing the uniform caches per shader in a simple array and linearly searching for them using 
// the uniform name already proved to be surprisingly fast, yielding a speedup of about 33%.
// Retrieving the location for each uniform of a shader, storing it externally and subsequently 
// using it directly and also as index of the related uniform value cache brought a speedup of
// about 50% (debug code), which I consider quite significant for something that simple.

class Shader 
{
    public:
        GLuint          m_handle;
        String          m_name;
        String          m_vs;
        String          m_fs;
        ManagedArray<UniformHandle*>    m_uniforms;
        ShaderLocationTable             m_locations;

        using KeyType = String;

        Shader(String name = "", String vs = "", String fs = "") : 
            m_handle(0), m_name(name) 
        { 
            // always resize m_uniforms so that any index passed to its operator[] will be valid (i.e. resize the underlying std::vector if needed)
            m_uniforms.SetAutoFit(true);
            // due to the randomness of uniform location retrieval, it must be made sure that the uniform variable cache list is never shrunk
            m_uniforms.SetShrinkable(false);
            // default value for automatic resizing
            m_uniforms.SetDefaultValue(nullptr);
        }

        Shader(const Shader& other) {
            m_handle = other.m_handle;
            m_uniforms = other.m_uniforms;
        }

        Shader (Shader&& other) noexcept {
            m_handle = std::exchange(other.m_handle, 0);
            m_uniforms = std::move(other.m_uniforms);
        }

        ~Shader () {
            Destroy ();
        }

        Shader& operator=(Shader&& other) noexcept {
            m_handle = other.m_handle;
            other.m_handle = 0;
            return *this;
        }

        String& GetKey(void) noexcept {
            return m_name;
        }

        String GetInfoLog (GLuint handle, bool isProgram = false);

        void PrintLog(String infoLog, String title);
            
        void PrintShaderSource(GLuint handle, String title);

        GLuint Compile(const char* code, GLuint type);

        GLuint Link(GLuint vsHandle, GLuint fsHandle);

        inline bool Create(const String& vsCode, const String& fsCode) {
            m_handle = Link(Compile((const char*)vsCode, GL_VERTEX_SHADER), Compile((const char*)fsCode, GL_FRAGMENT_SHADER));
            return m_handle != 0;
        }


        inline void Destroy(void) {
            if (m_handle > 0) {
                glDeleteProgram(m_handle);
                m_handle = 0;
            }
        }


        inline GLuint Handle(void) {
            return m_handle;
        }


        inline bool HaveBuffer(GLint location) noexcept {
            if (static_cast<int32_t>(location) >= m_uniforms.Length()) {
                try {
                    m_uniforms.Resize(location + 1);
                }
                catch (...) {
                    return false;
                }
            }
            return true;
        }


        template <typename UNIFORM_T>
        inline UNIFORM_T* GetUniform(const char* name, GLint* location) noexcept {
            if (*location >= 0) { // location has been successfully retrieved from this shader
                if (HaveBuffer(*location))
                    return dynamic_cast<UNIFORM_T*>(m_uniforms[*location]); // return uniform variable cache
            }
            else {
                if (*location < -1) // no location has yet been retrieved for this uniform
                    *location = glGetUniformLocation(m_handle, name); // retrieve it
                if (*location < 0)  // not present in current shader
                    return nullptr;
            }
            if (not HaveBuffer(*location)) // at this point, we at least have a location (>= 0) or know that it doesn't exist (-1)
                return nullptr; // this may allow caller to use regular gl call without buffering
            if (m_uniforms[*location] == nullptr) {// location has never been accessed before
                try {
                    m_uniforms[*location] = new UNIFORM_T(name, *location); // auto fit must be on for m_uniforms
                }
                catch (...) {
                    return nullptr;
                }
            }
            return static_cast<UNIFORM_T*>(m_uniforms[*location]);
        }


        template <typename DATA_T, typename UNIFORM_T>
        bool UpdateUniform(const char* name, GLint* location, DATA_T data) noexcept {
            bool initialize = *location < -1;
            UNIFORM_T* uniform = GetUniform<UNIFORM_T>(name, location);
            if (*location < 0)
                return false;
            if (not uniform)
                return false;
            if (*uniform == data)
                return initialize;
            *uniform = data;
            return true;
        }


        // -----------------------------------------------------------------------------------------
        // Automatically deduce proper glUniform function for passing uniform (array) data from data type passed

        template<typename DATA_T>
        GLint SetUniform(const char* name, DATA_T data) noexcept {
            using T = std::remove_cvref_t<DATA_T>;
            static_assert(std::is_same_v<T, int> || std::is_same_v<T, float>);

#if CACHE_SHADER_DATA
            GLint* location = m_locations[name];           // einmal ziehen, siehe Kommentar
            if (location == 0) 
                return -1;

            if (UpdateUniform<T, UniformData<T>>(name, location, static_cast<T>(data))) {
                if constexpr (std::is_same_v<T, int>)
                    glUniform1i(*location, static_cast<GLint>(data));
                else
                    glUniform1f(*location, static_cast<GLfloat>(data));
            }
            return *location;
#else
            GLint location = glGetUniformLocation(m_handle, name);
            if (location >= 0) {
                if constexpr (std::is_same_v<T, int>)
                    glUniform1i(location, static_cast<GLint>(data));
                else
                    glUniform1f(location, static_cast<GLfloat>(data));
            }
            return location;
#endif
        }

        // -----------------------------------------------------------------------------------------

        template <typename DATA_T>
        GLint SetUniformArray(const char* name, const DATA_T* data, size_t length) noexcept {
            using Base = ScalarBaseType<DATA_T>;          // statt: std::remove_cv_t<typename ScalarBaseType<DATA_T>>
            constexpr int C = ComponentCount<DATA_T>;

            static_assert(std::is_same_v<Base, float> || std::is_same_v<Base, int>, "only float or int");
            static_assert(C >= 1 && C <= 4, "components 1..4");

#if CACHE_SHADER_DATA
            GLint* location = m_locations[name];
            if (not location) 
                return -1;
            if (UpdateUniform<const DATA_T*, UniformArray<DATA_T>>(name, location, data)) 
                UniformFuncs::gl_uniform<Base, C>::fn(*location, GLsizei(length), reinterpret_cast<const Base*>(data));
            return *location;
#else
            GLint location = glGetUniformLocation(m_handle, name);
            if (location >= 0) 
                UniformFuncs::gl_uniform<Base, C>::fn(location, GLsizei(length), reinterpret_cast<const Base*>(data));
            return location;
#endif
        }

        // -----------------------------------------------------------------------------------------

        GLint SetMatrix4f(const char* name, const float* data, bool transpose = false)
            noexcept;

        inline GLint SetMatrix4f(const char* name, ManagedArray<GLfloat>& data, bool transpose = false) noexcept {
            return SetMatrix4f(name, data.Data(), transpose);
        }

        GLint SetMatrix3f(const char* name, float* data, bool transpose = false)
            noexcept;

        inline GLint SetMatrix3f(const char* name, ManagedArray<GLfloat>& data, bool transpose) noexcept {
            return SetMatrix3f(name, data.Data(), transpose);
        }

        inline GLint SetInt(const char* name, int data) noexcept {
            return SetUniform<int>(name, data);
        }

        inline GLint SetFloat(const char* name, float data) noexcept {
            return SetUniform<float>(name, data);
        }

        inline GLint SetVector2fArray(const char* name, const Vector2f* data, GLsizei length) noexcept {
            return SetUniformArray<Vector2f>(name, data, length);
        }

        inline GLint SetVector2f(const char* name, const Vector2f& data) noexcept {
            return SetUniformArray<Vector2f>(name, &data, 1);
        }

        inline GLint SetVector2f(const char* name, Vector2f&& data) noexcept {
            return SetVector2f(name, static_cast<const Vector2f&>(data));
        }

        inline GLint SetVector2f(const char* name, float x, float y) noexcept {
            return SetVector2f(name, Vector2f(x, y));
        }

        inline GLint SetVector3fArray(const char* name, const Vector3f* data, GLsizei length) noexcept {
            return SetUniformArray<Vector3f>(name, data, length);
        }

        inline GLint SetVector3f(const char* name, const Vector3f& data) noexcept {
            return SetUniformArray<Vector3f>(name, &data, 1);
        }

        inline GLint SetVector3f(const char* name, Vector3f&& data) noexcept {
            return SetVector3f(name, static_cast<const Vector3f&>(data));
        }

        inline GLint SetVector4fArray(const char* name, const Vector4f* data, GLsizei length) noexcept {
            return SetUniformArray<Vector4f>(name, data, length);
        }

        inline GLint SetVector4f(const char* name, const Vector4f& data) noexcept {
            return SetUniformArray<Vector4f>(name, &data, 1);
        }

        inline GLint SetVector4f(const char* name, Vector4f&& data) noexcept {
            return SetVector4f(name, static_cast<const Vector4f&>(data));
        }

        inline GLint SetVector2i(const char* name, const Vector2i& data) noexcept {
            return SetUniformArray<Vector2i>(name, &data, 1);
        }

        inline GLint SetVector2i(const char* name, Vector2i&& data) noexcept {
            return SetVector2i(name, static_cast<const Vector2i&>(data));
        }

        inline GLint SetVector3i(const char* name, const Vector3i& data) noexcept {
            return SetUniformArray<Vector3i>(name, &data, 1);
        }

        inline GLint SetVector2i(const char* name, Vector3i&& data) noexcept {
            return SetVector3i(name, static_cast<const Vector3i&>(data));
        }

        inline GLint SetVector4i(const char* name, const Vector4i& data) noexcept {
            return SetUniformArray<Vector4i>(name, &data, 1);
        }

        inline GLint SetVector4i(const char* name, Vector4i&& data) noexcept {
            return SetVector4i(name, static_cast<const Vector4i&>(data));
        }

        inline GLint SetFloatArray(const char* name, const float* data, size_t length) noexcept {
            return SetUniformArray<float>(name, data, length);
        }

        inline GLint SetFloatArray(const char* name, const FloatArray& data) noexcept {
            return SetUniformArray<float>(name, data.Data(), data.Length());
        }

        // -----------------------------------------------------------------------------------------

        static inline float* GetFloatData(GLenum id, int32_t size, float* data) noexcept {
            glGetFloatv(id, (GLfloat*)data);
            return data;
        }

        static inline ManagedArray<float>& GetFloatData(GLenum id, int32_t size, ManagedArray<float>& glData) noexcept {
            if (glData.Length() < size)
                try {
                glData.Resize(size);
            }
            catch (...)
            {
                return glData;
            }
            GetFloatData(id, size, glData.Data());
            return glData;
        }

        inline void Enable(void) {
            glUseProgram(m_handle);
        }

        inline void Disable(void) {
            glUseProgram(0);
        }

        void UpdateMatrices(void);

        inline const bool operator< (String const& name) const { return m_name < name; }

        bool operator> (const String& name) const { return m_name > name; }

        bool operator<= (const String& name) const { return m_name <= name; }

        bool operator>= (const String& name) const { return m_name >= name; }

        bool operator!= (const String& name) const { return m_name != name; }

        bool operator== (const String& name) const { return m_name == name; }

        bool operator< (const Shader& other) const { return m_name < other.m_name; }

        bool operator> (const Shader& other) const { return m_name > other.m_name; }

        bool operator<= (const Shader& other) const { return m_name <= other.m_name; }

        bool operator>= (const Shader& other) const { return m_name >= other.m_name; }

        bool operator!= (const Shader& other) const { return m_name != other.m_name; }

        bool operator== (const Shader& other) const { return m_name == other.m_name; }
};

// =================================================================================================
