
#include <utility>
#include <string_view>
#include <ranges>

#include "shader.h"
#include "shadowmap.h"
#include "base_renderer.h"

#define PASSTHROUGH_MODE 0

// =================================================================================================
// Some basic shader handling: Compiling, enabling, setting shader variables

void Shader::PrintLog(String infoLog, String title) {
#ifdef _DEBUG
    // Gesamtzahl der Zeilen zählen
    const size_t lineCount = std::ranges::count(infoLog, '\n') + 1;
    const int width = static_cast<int>(std::to_string(lineCount).size());

    fprintf(stderr, "\n%s\n", (char*) title);

    int lineNo = 0;
    for (auto&& chunk : infoLog | std::views::split('\n')) {
        std::string_view line(chunk.begin(), chunk.end());
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        fprintf(stderr, "%3d: %.*s\n", ++lineNo, static_cast<int>(line.size()), line.data());
    }
    fprintf(stderr, "\n\n");
#endif
}


void Shader::PrintShaderSource(GLuint handle, String title) {
    String buffer;
    GLsizei bufLen = 0;
    glGetShaderiv(handle, GL_SHADER_SOURCE_LENGTH, &bufLen);
    buffer.Resize(bufLen);
    glGetShaderSource(handle, bufLen, &bufLen, buffer.Data());
    PrintLog(buffer, title);
}


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
    PrintLog(infoLog, isProgram ? String("Shader program") : String("Shader"));
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
#ifdef _DEBUG
    String shaderLog = GetInfoLog(handle);
    fprintf(stderr, "\n***** GLSL compiler error in %s shader: *****\n\n", (char*)m_name);
    PrintShaderSource(handle, String("Shader source:"));
#endif
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
#ifdef _DEBUG
    String shaderLog = GetInfoLog(handle, true);
    fprintf(stderr, "\n***** GLSL linker error in %s shader: *****\n\n", (char*)m_name);
    PrintShaderSource(vsHandle, String("Vertex shader:"));
    PrintShaderSource(fsHandle, String("Fragment shader:"));
#endif
    glDeleteShader(vsHandle);
    glDeleteShader(fsHandle);
    glDeleteProgram(handle);
    return 0;
}

// set modelview, projection and viewport matrices in shader. 
// Every shader program should have at least modelview and projection matrices.
// Also starts location indexing by calling m_locations.Start().
void Shader::UpdateMatrices(void) {
    m_locations.Start();
#ifdef _DEBUG
    if (RenderMatrices::LegacyMode) {
        float glData[16];
        SetMatrix4f("mModelView", GetFloatData(GL_MODELVIEW_MATRIX, 16, glData));
        SetMatrix4f("mProjection", GetFloatData(GL_PROJECTION_MATRIX, 16, glData));
    }
#endif
    else 
    {
        // both matrices must be column major
        if (not baseRenderer.IsShadowPass()) {
            SetMatrix4f("mModelView", baseRenderer.ModelView().AsArray(), false);
            SetMatrix4f("mProjection", baseRenderer.Projection().AsArray(), false);
            SetMatrix4f("mViewport", baseRenderer.ViewportTransformation().AsArray(), false);
            SetInt("renderShadow", shadowMap.IsReady() ? 1 : 0);
        }
        if (shadowMap.IsReady())
            SetMatrix4f("mShadowTransform", shadowMap.ShadowTransform());
#if 0
        SetMatrix4f("mBaseModelView", baseRenderer.ModelView().AsArray(), false);
#endif
    }
#if 0
    baseRenderer.CheckModelView();
    baseRenderer.CheckProjection();
    float glmData[16];
    memcpy(glmData, baseRenderer.Projection().AsArray(), sizeof(glmData));
#endif
}


GLint Shader::SetMatrix4f(const char* name, const float* data, bool transpose) noexcept {
    GLint* location = m_locations[name]; // .Current();
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (*location >= 0)
        glUniformMatrix4fv(*location, 1, GLboolean(transpose), data);
    return location;
#else
    if (UpdateUniform<const float*, UniformArray16f>(name, location, data))
        glUniformMatrix4fv(*location, 1, GLboolean(transpose), data);
    return *location;
#endif
}


GLint Shader::SetMatrix3f(const char* name, float* data, bool transpose) noexcept {
    GLint* location = m_locations[name]; // .Current();
#if PASSTHROUGH_MODE
    GetLocation(name, location);
    if (*location >= 0)
        glUniformMatrix3fv(*location, 1, GLboolean(transpose), data);
    return location;
#else
    if (UpdateUniform<float*, UniformArray9f>(name, location, data))
        glUniformMatrix3fv(*location, 1, GLboolean(transpose), data);
    return *location;
#endif
}

// =================================================================================================
