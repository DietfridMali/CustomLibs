#pragma once

#include <type_traits>
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

        String& GetKey(void) {
            return m_name;
        }

        String GetInfoLog (GLuint handle, bool isProgram = false);
            
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

        inline GLint GetLocation(const char* name, GLint& location) const {
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


        template <typename T>
        inline T* GetUniform(const char* name, GLint& location) {
            if (location >= 0) // location has been successfully retrieved from this shader
                return dynamic_cast<T*>(m_uniforms[location]); // return uniform variable cache
            if (location < -1) // no location has yet been retrieved for this uniform
                location = glGetUniformLocation(m_handle, name); // retrieve it
            if (location < 0) // not present in current shader
                return nullptr;
            if (m_uniforms[location] == nullptr) // location has never been accessed before
                m_uniforms[location] = new T(name, location); // auto fit must be on for m_uniforms
            return dynamic_cast<T*>(m_uniforms[location]);
        }


        GLint SetMatrix4f(const char* name, GLint& location, const float* data, bool transpose = false);

        inline GLint SetMatrix4f(const char* name, GLint& location, ManagedArray<GLfloat>& data, bool transpose = false) {
            return SetMatrix4f(name, location, data.Data(), transpose);
        }

        GLint SetMatrix3f(const char* name, GLint& location, float* data, bool transpose = false);

        inline GLint SetMatrix3f(const char* name, GLint& location, ManagedArray<GLfloat>& data, bool transpose) {
            SetMatrix3f(name, location, data.Data(), transpose);
        }

        GLint SetVector4f(const char* name, GLint& location, const Vector4f& data);

        inline GLint SetVector4f(const char* name, GLint& location, Vector4f&& data) {
            return SetVector4f(name, location, static_cast<const Vector4f&>(data));
        }

        GLint SetVector3f(const char* name, GLint& location, const Vector3f& data);

        inline GLint SetVector3f(const char* name, GLint& location, Vector3f&& data) {
            return SetVector3f(name, location, static_cast<const Vector3f&>(data));
        }

        GLint SetVector2f(const char* name, GLint& location, const Vector2f& data);

        inline GLint SetVector2f(const char* name, GLint& location, Vector2f&& data) {
            return SetVector2f(name, location, static_cast<const Vector2f&>(data));
        }

        inline GLint SetVector2f(const char* name, GLint& location, float x, float y) {
            return SetVector2f(name, location, Vector2f(x, y));
        }

        GLint SetFloat(const char* name, GLint& location, float data);

        GLint SetVector2i(const char* name, GLint& location, const GLint* data);

        GLint SetVector3i(const char* name, GLint& location, const GLint* data);

        GLint SetVector4i(const char* name, GLint& location, const GLint* data);

        GLint SetInt(const char* name, GLint& location, int data);

        GLint SetFloatData(const char* name, GLint& location, const float* data, size_t length);

        inline GLint SetFloatData(const char* name, GLint& location, const FloatArray& data) {
            return SetFloatData(name, location, data.Data(), size_t(data.Length()));
        }

        GLint SetIntData(const char* name, GLint& location, const int* data, size_t length);

        inline GLint SetIntData(const char* name, GLint& location, const IntArray& data) {
            return SetIntData(name, location, data.Data(), size_t(data.Length()));
        }

        static inline float* GetFloatData(GLenum id, int32_t size, float* data) {
            glGetFloatv(id, (GLfloat*)data);
            return data;
        }

        static inline ManagedArray<float>& GetFloatData(GLenum id, int32_t size, ManagedArray<float>& glData) {
            if (glData.Length() < size)
                glData.Resize(size);
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
