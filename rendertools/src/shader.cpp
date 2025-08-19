
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



GLint Shader::SetMatrix4f(const char* name, GLint& location, const float* data, bool transpose) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniformMatrix4fv(location, 1, GLboolean(transpose), data);
    return location;
#else
    UniformArray16f* uniform = GetUniform<UniformArray16f>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        uniform->Copy(data);
        glUniformMatrix4fv(uniform->m_location, 1, GLboolean(transpose), data);
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetMatrix3f(const char* name, GLint& location, float* data, bool transpose) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniformMatrix3fv(location, 1, GLboolean(transpose), data);
    return location;
#else
    UniformArray9f* uniform = GetUniform<UniformArray9f>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        uniform->Copy(data);
        glUniformMatrix3fv(uniform->m_location, 1, GLboolean(transpose), data);
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetVector4f(const char* name, GLint& location, const Vector4f& data) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform4fv(location, 1, data.Data());
    return location;
#else
    UniformVector4f* uniform = GetUniform<UniformVector4f>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        *uniform = data;
        glUniform4fv(uniform->m_location, 1, data.Data());
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetVector3f(const char* name, GLint& location, const Vector3f& data) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform3fv(location, 1, data.Data());
    return location;
#else
    UniformVector3f* uniform = GetUniform<UniformVector3f>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        *uniform = data;
        glUniform3fv(uniform->m_location, 1, data.Data());
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetVector2f(const char* name, GLint& location, const Vector2f& data) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform2fv(location, 1, data.Data());
    return location;
#else
    UniformVector2f* uniform = GetUniform<UniformVector2f>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        *uniform = data;
        glUniform2fv(uniform->m_location, 1, data.Data());
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetFloat(const char* name, GLint& location, float data) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform1f(location, GLfloat(data));
    return location;
#else
    UniformFloat* uniform = GetUniform<UniformFloat>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        *uniform = data;
        glUniform1f(uniform->m_location, GLfloat(data));
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetVector2i(const char* name, GLint& location, const GLint* data) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform2iv(location, 1, data);
    return location;
#else
    UniformArray2i* uniform = GetUniform<UniformArray2i>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        *uniform = data;
        glUniform2iv(uniform->m_location, 1, data);
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetVector3i(const char* name, GLint& location, const GLint* data) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform3iv(location, 1, data);
    return location;
#else
    UniformArray3i* uniform = GetUniform<UniformArray3i>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        *uniform = data;
        glUniform3iv(uniform->m_location, 1, data);
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetVector4i(const char* name, GLint& location, const GLint* data) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform4iv(location, 1, data);
    return location;
#else
    UniformArray4i* uniform = GetUniform<UniformArray4i>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        *uniform = data;
        glUniform4iv(uniform->m_location, 1, data);
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetInt(const char* name, GLint& location, int data) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform1i(location, GLint(data));
    return location;
#else
    UniformInt* uniform = GetUniform<UniformInt>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        *uniform = data;
        glUniform1i(uniform->m_location, GLint(data));
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetFloatData(const char* name, GLint& location, const float* data, size_t length) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform1fv(location, GLsizei(length), reinterpret_cast<const GLfloat*>(data));
    return location;
#else
    UniformArray<float>* uniform = GetUniform<UniformArray<float>>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        uniform->Copy(data, length);
        glUniform1fv(uniform->m_location, GLsizei(length), reinterpret_cast<const GLfloat*>(data));
    }
    return uniform->m_location;
#endif
}


GLint Shader::SetIntData(const char* name, GLint& location, const int* data, size_t length) {
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (location >= 0)
        glUniform1iv(location, GLsizei(length), reinterpret_cast<const GLint*>(data));
    return location;
#else
    UniformArray<int>* uniform = GetUniform<UniformArray<int>>(name, location);
    if (not uniform or (uniform->m_location < 0))
        return -1;
    if (*uniform != data) {
        uniform->Copy(data, length);
        glUniform1iv(uniform->m_location, GLsizei(length), reinterpret_cast<const GLint*>(data));
    }
    return uniform->m_location;
#endif
}

// =================================================================================================
