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

#define LOCATION_LOOKUP_MODE 0

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

        inline GLint GetLocation(const char* name) const {
         // location is returned back to caller of SetUniform method who stores is for future use
         // initially, that caller sets location to a value < -1 to signal that it has to be initialized here
         // so if location < -1, return glGetUnifomLocation result, otherwise location has been initialized; just return it
#if LOCATION_LOOKUP_MODE == 0
            location = glGetUniformLocation(m_handle, name);
            if (location == -1)
                fprintf(stderr, "location %s::%s not found\n", (const char*)m_name, (const char*)name);
            return location;
#else
            return (location < -1) ? glGetUniformLocation(m_handle, name) : location;
#endif
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
        inline UNIFORM_T* GetUniform(const char* name, GLint& location) noexcept {
            if (location >= 0) { // location has been successfully retrieved from this shader
                if (HaveBuffer(location))
                    return dynamic_cast<UNIFORM_T*>(m_uniforms[location]); // return uniform variable cache
            }
            else {
                if (location < -1) // no location has yet been retrieved for this uniform
                    location = glGetUniformLocation(m_handle, name); // retrieve it
                if (location < 0)  // not present in current shader
                    return nullptr;
            }
            if (not HaveBuffer(location)) // at this point, we at least have a location (>= 0) or know that it doesn't exist (-1)
                return nullptr; // this may allow caller to use regular gl call without buffering
            if (m_uniforms[location] == nullptr) {// location has never been accessed before
                try {
                    m_uniforms[location] = new UNIFORM_T(name, location); // auto fit must be on for m_uniforms
                }
                catch (...) {
                    return nullptr;
                }
            }
            return static_cast<UNIFORM_T*>(m_uniforms[location]);
        }


        template <typename DATA_T, typename UNIFORM_T>
        bool UpdateUniform(const char* name, GLint& location, DATA_T data) noexcept {
            UNIFORM_T* uniform = GetUniform<UNIFORM_T>(name, location);
            if (location < 0)
                return false;
            if (not uniform)
                return false;
            if (*uniform == data)
                return false;
            *uniform = data;
            return true;
        }


        // -----------------------------------------------------------------------------------------
        // Automatically deduce proper glUniform function for passing uniform (array) data from data type passed

        template <typename S> struct glUniform1;

        template <> struct glUniform1<int> {
            using fn_t = PFNGLUNIFORM1IPROC;
            static inline fn_t& fn = glUniform1i;
        };

        template <> struct glUniform1<float> {
            using fn_t = PFNGLUNIFORM1FPROC;
            static inline fn_t& fn = glUniform1f; // referenziert den echten Loader-Zeiger
        };

        template <typename DATA_T>
        GLint SetUniform(const char* name, DATA_T data) noexcept {
            static_assert(
                std::is_same_v<std::remove_cv_t<DATA_T>, int> or 
                std::is_same_v<std::remove_cv_t<DATA_T>, float>
                );

            location = m_locations.Current(); // call only once per SetUniform call because it switches the internal index of m_locations to the next entry!
            GetLocation(name, location);
            if (location < 0) 
                return location;

            using FuncType = glUniform1<DATA_T>;

#if PASSTHROUGH_MODE
            FuncType::fn(location, data);
#else
            // Cache: speichere genau den externen Typ DATA_T
            if (UpdateUniform<DATA_T, UniformData<DATA_T>>(name, location, data)) {
                FuncType::fn(location, data);
            }
#endif
            return location;
        }

        // -----------------------------------------------------------------------------------------

        // 2) Dispatcher: S∈{float,int}, K∈{1..4} → passender glUniform-Zeiger
        template <typename S, int C> struct glUniform;

        // Variante A: Referenzen auf Loader-Symbole (GLAD/GLEW)
        template <typename, int> struct glUniform;

        // float-Kanal
        template <> struct glUniform<float, 1> {
            using GlFuncType = PFNGLUNIFORM1FVPROC;
            static inline GlFuncType& fn = glUniform1fv;
        };

        template <> struct glUniform<float, 2> {
            using GlFuncType = PFNGLUNIFORM2FVPROC;
            static inline GlFuncType& fn = glUniform2fv;
        };

        template <> struct glUniform<float, 3> {
            using GlFuncType = PFNGLUNIFORM3FVPROC;
            static inline GlFuncType& fn = glUniform3fv;
        };

        template <> struct glUniform<float, 4> {
            using GlFuncType = PFNGLUNIFORM4FVPROC;
            static inline GlFuncType& fn = glUniform4fv;
        };

        // int-Kanal
        template <> struct glUniform<int, 1> {
            using GlFuncType = PFNGLUNIFORM1IVPROC;
            static inline GlFuncType& fn = glUniform1iv;
        };

        template <> struct glUniform<int, 2> {
            using GlFuncType = PFNGLUNIFORM2IVPROC;
            static inline GlFuncType& fn = glUniform2iv;
        };

        template <> struct glUniform<int, 3> {
            using GlFuncType = PFNGLUNIFORM3IVPROC;
            static inline GlFuncType& fn = glUniform3iv;
        };

        template <> struct glUniform<int, 4> {
            using GlFuncType = PFNGLUNIFORM4IVPROC;
            static inline GlFuncType& fn = glUniform4iv;
        };

        template <typename DATA_T>
        GLint SetUniformArray(const char* name, const DATA_T* data, size_t length) noexcept
        {
            using BaseType = ScalarBaseType<DATA_T>;
            constexpr int Components = ComponentCount<DATA_T>;

            static_assert((std::is_same_v<BaseType, float> or std::is_same_v<BaseType, int>), "only float and int base types possible");
            static_assert(Components >= 1 and Components <= 4, "only 1 to 4 components possible");

            location = m_locations.Current(); // call only once per SetUniformArray call because it switches the internal index of m_locations to the next entry!
            GetLocation(name, location);
            if (location < 0) 
                return location;

            using FuncType = glUniform<BaseType, Components>;
#if PASSTHROUGH_MODE
            FuncType::fn(location, GLsizei(length), reinterpret_cast<const BaseType*>(data));
#else
            // Cache: speichere genau den externen Typ DATA_T
            if (UpdateUniform<const DATA_T*, UniformArray<DATA_T>>(name, location, data)) {
                FuncType::fn(location, GLsizei(length), reinterpret_cast<const BaseType*>(data));
            }
#endif
            return location;
        }

        // -----------------------------------------------------------------------------------------

        GLint SetMatrix4f(const char* name, const float* data, bool transpose = false)
            noexcept;

        inline GLint SetMatrix4f(const char* name, ManagedArray<GLfloat>& data, bool transpose = false) noexcept {
            return SetMatrix4f(name, location, data.Data(), transpose);
        }

        GLint SetMatrix3f(const char* name, float* data, bool transpose = false)
            noexcept;

        inline GLint SetMatrix3f(const char* name, ManagedArray<GLfloat>& data, bool transpose) noexcept {
            SetMatrix3f(name, location, data.Data(), transpose);
        }
#if 1
        GLint SetInt(const char* name, int data) noexcept {
            return SetUniform<int>(name, location, data);
        }

        GLint SetFloat(const char* name, float data) noexcept {
            return SetUniform<float>(name, location, data);
        }

        GLint SetVector4f(const char* name, const Vector4f& data) noexcept {
            return SetUniformArray<Vector4f>(name, location, &data, 1);
        }

        inline GLint SetVector4f(const char* name, Vector4f&& data) noexcept {
            return SetVector4f(name, location, static_cast<const Vector4f&>(data));
        }

        GLint SetVector3f(const char* name, const Vector3f& data) noexcept {
            return SetUniformArray<Vector3f>(name, location, &data, 1);
        }

        inline GLint SetVector3f(const char* name, Vector3f&& data) noexcept {
            return SetVector3f(name, location, static_cast<const Vector3f&>(data));
        }

        GLint SetVector2f(const char* name, const Vector2f& data) noexcept {
            return SetUniformArray<Vector2f>(name, location, &data, 1);
        }

        inline GLint SetVector2f(const char* name, Vector2f&& data) noexcept {
            return SetVector2f(name, location, static_cast<const Vector2f&>(data));
        }

        inline GLint SetVector2f(const char* name, float x, float y) noexcept {
            return SetVector2f(name, location, Vector2f(x, y));
        }

        GLint SetVector2i(const char* name, const GLint* data) noexcept {
            return SetUniformArray<int>(name, location, data, 2);
        }

        GLint SetVector3i(const char* name, const GLint* data) noexcept {
            return SetUniformArray<int>(name, location, data, 3);
        }

        GLint SetVector4i(const char* name, const GLint* data) noexcept {
            return SetUniformArray<int>(name, location, data, 4);
        }

        GLint SetFloatArray(const char* name, const float* data, size_t length) noexcept {
            return SetUniformArray<float>(name, location, data, length);
        }
#endif

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
