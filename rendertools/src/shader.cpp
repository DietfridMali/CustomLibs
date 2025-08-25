
#include <utility>
#include "shader.h"
#include "base_renderer.h"

#define PASSTHROUGH_MODE 0

// =================================================================================================
// Some basic shader handling: Compiling, enabling, setting shader variables

String Shader::GetInfoLog (GLuint handle, bool isProgram)
{
    int logLength = 0;
    int charsWritten = 0;

    if (isProgram)
        glGetProgramiv (handle, GL_INFO_LOG_LENGTH, &logLength);
    else
        glGetShaderiv (handle, GL_INFO_LOG_LENGTH, &logLength);

    if (not logLength)
        return String ("no log found.\n");
    String infoLog;
    infoLog.Resize(logLength);
    if (isProgram)
        glGetProgramInfoLog (handle, logLength, &charsWritten,infoLog.Data());
    else
        glGetShaderInfoLog (handle, logLength, &charsWritten, infoLog.Data());
    fprintf (stderr, "Shader info: %s\n\n", static_cast<char*>(infoLog));
    return infoLog;
    }


GLuint Shader::Compile(const char* code, GLuint type) {
    GLuint handle = glCreateShader(type);
    glShaderSource(handle, 1, (GLchar**)&code, nullptr);
    glCompileShader(handle);
    GLint isCompiled;
    glGetShaderiv (handle, GL_COMPILE_STATUS, &isCompiled);
    if (isCompiled == GL_TRUE)
        return handle;
    String shaderLog = GetInfoLog (handle);
    char buffer [10000];
    GLsizei bufLen;
    glGetShaderSource (handle, sizeof (buffer), &bufLen, buffer);
    fprintf(stderr, "\n***** compiler error in %s shader: *****\n\n", (char*)m_name);
    fprintf (stderr, "\nshader source:\n%s\n\n", buffer);
    glDeleteShader(handle);
    return 0;
}


GLuint Shader::Link(GLuint vsHandle, GLuint fsHandle) {
    if (not vsHandle or not fsHandle)
        return 0;
    GLuint handle = glCreateProgram();
    if (not handle)
        return 0;
    glAttachShader(handle, vsHandle);
    glAttachShader(handle, fsHandle);
    glLinkProgram(handle);
    GLint isLinked = 0;
    glGetProgramiv(handle, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_TRUE) {
        glDetachShader(handle, vsHandle);
        glDetachShader(handle, fsHandle);
        return handle;
    }
    String shaderLog = GetInfoLog (handle, true);
    char buffer [10000];
    GLsizei bufLen;
    fprintf(stderr, "\n***** linker error in %s shader: *****\n\n", (char*)m_name);
    glGetShaderSource (vsHandle, sizeof (buffer), &bufLen, buffer);
    fprintf (stderr, "\nVertex shader:\n%s\n\n", buffer);
    glGetShaderSource (fsHandle, sizeof (buffer), &bufLen, buffer);
    fprintf (stderr, "\nFragment shader:\n%s\n\n", buffer);
    glDeleteShader(vsHandle);
    glDeleteShader(fsHandle);
    glDeleteProgram(handle);
    return 0;
}


void Shader::UpdateMatrices(void) {
    m_locations.Start();
    float glData[16];
    if (RenderMatrices::m_legacyMode) {
        SetMatrix4f("mModelView", m_locations.Current(), GetFloatData(GL_MODELVIEW_MATRIX, 16, glData));
        SetMatrix4f("mProjection", m_locations.Current(), GetFloatData(GL_PROJECTION_MATRIX, 16, glData));
    }
    else {
        // both matrices must be column major
        SetMatrix4f("mModelView", m_locations.Current(), baseRenderer.ModelView().AsArray(), false);
        SetMatrix4f("mProjection", m_locations.Current(), baseRenderer.Projection().AsArray(), false);
#if 0
        SetMatrix4f("mBaseModelView", m_locations.Current(), baseRenderer.ModelView().AsArray(), false);
#endif
    }
#if 0
    baseRenderer.CheckModelView();
    baseRenderer.CheckProjection();
    float glmData[16];
    memcpy(glmData, baseRenderer.Projection().AsArray(), sizeof(glmData));
#endif
}


GLint Shader::SetMatrix4f(const char* name, GLint& location, const float* data, bool transpose) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniformMatrix4fv(location, 1, GLboolean(transpose), data);
    return location;
#else
    if (UpdateUniform<const float*, UniformArray16f>(name, location, data))
        UniformArray16f* uniform = GetUniform<UniformArray16f>(name, location);
    glUniformMatrix4fv(location, 1, GLboolean(transpose), data);
    return location;
#endif
}


GLint Shader::SetMatrix3f(const char* name, GLint& location, float* data, bool transpose) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniformMatrix3fv(location, 1, GLboolean(transpose), data);
    return location;
#else
    if (UpdateUniform<float*, UniformArray9f>(name, location, data))
        glUniformMatrix3fv(location, 1, GLboolean(transpose), data);
    return location;
#endif
}


GLint Shader::SetVector4f(const char* name, GLint& location, const Vector4f& data) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform4fv(location, 1, data.Data());
    return location;
#else
    if (UpdateUniform<const Vector4f, UniformVector4f>(name, location, data))
        glUniform4fv(location, 1, data.Data());
    return location;
#endif
}


GLint Shader::SetVector3f(const char* name, GLint& location, const Vector3f& data) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform3fv(location, 1, data.Data());
    return location;
#else
    if (UpdateUniform<const Vector3f, UniformVector3f>(name, location, data))
        glUniform3fv(location, 1, data.Data());
    return location;
#endif
}


GLint Shader::SetVector2f(const char* name, GLint& location, const Vector2f& data) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform2fv(location, 1, data.Data());
    return location;
#else
    if (UpdateUniform<const Vector2f, UniformVector2f>(name, location, data))
        glUniform2fv(location, 1, data.Data());
    return location;
#endif
}


GLint Shader::SetFloat(const char* name, GLint& location, float data) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform1f(location, GLfloat(data));
    return location;
#else
    if (UpdateUniform<float, UniformFloat>(name, location, data))
        glUniform1f(location, GLfloat(data));
    return location;
#endif
}


GLint Shader::SetVector2i(const char* name, GLint& location, const GLint* data) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform2iv(location, 1, data);
    return location;
#else
    if (UpdateUniform<const GLint*, UniformArray2i>(name, location, data))
        glUniform2iv(location, 1, data);
    return location;
#endif
}


GLint Shader::SetVector3i(const char* name, GLint& location, const GLint* data) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform3iv(location, 1, data);
    return location;
#else
    if (UpdateUniform<const GLint*, UniformArray3i>(name, location, data))
        glUniform3iv(location, 1, data);
    return location;
#endif
}


GLint Shader::SetVector4i(const char* name, GLint& location, const GLint* data) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform4iv(location, 1, data);
    return location;
#else
    if (UpdateUniform<const GLint*, UniformArray4i>(name, location, data))
        glUniform4iv(location, 1, data);
    return location;
#endif
}


GLint Shader::SetInt(const char* name, GLint& location, int data) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform1i(location, GLint(data));
    return location;
#else
    if (UpdateUniform<int, UniformInt>(name, location, data))
        glUniform1i(location, GLint(data));
    return location;
#endif
}


GLint Shader::SetFloatData(const char* name, GLint& location, const float* data, size_t length) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform1fv(location, GLsizei(length), reinterpret_cast<const GLfloat*>(data));
    return location;
#else
    if (UpdateUniform<const float*, UniformArray<float>>(name, location, data))
        glUniform1fv(location, GLsizei(length), reinterpret_cast<const GLfloat*>(data));
    return location;
#endif
}


GLint Shader::SetIntData(const char* name, GLint& location, const int* data, size_t length) noexcept {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform1iv(location, GLsizei(length), reinterpret_cast<const GLint*>(data));
    return location;
#else
    if (UpdateUniform<const int*, UniformArray<int>>(name, location, data))
        glUniform1iv(location, GLsizei(length), reinterpret_cast<const GLint*>(data));
    return location;
#endif
}

// =================================================================================================
