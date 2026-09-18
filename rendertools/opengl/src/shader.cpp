
#include <utility>
#include <string_view>
#include <ranges>
#include <vector>

#include "shader.h"
#include "shadercache.h"
#include "shadowmap.h"
#include "gfxrenderer.h"

#define PASSTHROUGH_MODE 0

// =================================================================================================
// Some basic shader handling: Compiling, enabling, setting shader variables

void Shader::PrintLog(String infoLog, String title) {
#ifdef _DEBUG
    // Gesamtzahl der Zeilen z�hlen
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
    if (not code or not *code)
		return 0;
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


static void DeleteShaders(std::initializer_list<GLuint> handles) {
    for (GLuint handle : handles)
        if (handle)
            glDeleteShader(handle);
}


GLuint Shader::Link(GLuint vsHandle, GLuint fsHandle, GLuint gsHandle, GLuint tcsHandle, GLuint tesHandle) {
    m_isTessellated = false;
    if (not vsHandle or not fsHandle) {
        DeleteShaders({ vsHandle, fsHandle, gsHandle, tcsHandle, tesHandle });
        return 0;
    }
    // the tessellation stages come as a pair; a program with only one of them does not link
    if ((tcsHandle != 0) != (tesHandle != 0)) {
#ifdef _DEBUG
        fprintf(stderr, "\n***** %s shader: tessellation control and evaluation shader must both be present *****\n\n", (char*)m_name);
#endif
        DeleteShaders({ vsHandle, fsHandle, gsHandle, tcsHandle, tesHandle });
        return 0;
    }
    GLuint handle = glCreateProgram();
    if (not handle) {
        DeleteShaders({ vsHandle, fsHandle, gsHandle, tcsHandle, tesHandle });
        return 0;
    }
    glAttachShader(handle, vsHandle);
    glAttachShader(handle, fsHandle);
	if (gsHandle)
        glAttachShader(handle, gsHandle);
    if (tcsHandle) {
        glAttachShader(handle, tcsHandle);
        glAttachShader(handle, tesHandle);
    }
    glProgramParameteri(handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glLinkProgram(handle);
    GLint isLinked = 0;
    glGetProgramiv(handle, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_TRUE) {
        glDetachShader(handle, vsHandle);
        glDetachShader(handle, fsHandle);
        if (gsHandle)
            glDetachShader(handle, gsHandle);
        if (tcsHandle) {
            glDetachShader(handle, tcsHandle);
            glDetachShader(handle, tesHandle);
        }
        DeleteShaders({ vsHandle, fsHandle, gsHandle, tcsHandle, tesHandle });
        m_isTessellated = tcsHandle != 0;
        return handle;
    }
#ifdef _DEBUG
    String shaderLog = GetInfoLog(handle, true);
    fprintf(stderr, "\n***** GLSL linker error in %s shader: *****\n\n", (char*)m_name);
    PrintShaderSource(vsHandle, String("Vertex shader:"));
    if (tcsHandle) {
        PrintShaderSource(tcsHandle, String("Tessellation control shader:"));
        PrintShaderSource(tesHandle, String("Tessellation evaluation shader:"));
    }
    PrintShaderSource(fsHandle, String("Fragment shader:"));
#endif
    glDeleteShader(vsHandle);
    glDeleteShader(fsHandle);
    if (gsHandle)
		glDeleteShader(gsHandle);
    if (tcsHandle) {
        glDeleteShader(tcsHandle);
        glDeleteShader(tesHandle);
    }
    glDeleteProgram(handle);
    return 0;
}


static bool HaveProgramBinaryFormat(GLenum format) {
    GLint formatCount = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formatCount);
    if (formatCount <= 0)
        return false;
    std::vector<GLint> formats(size_t(formatCount), 0);
    glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, formats.data());
    for (GLint f : formats)
        if (GLenum(f) == format)
            return true;
    return false;
}


uint64_t Shader::ProgramKey(std::initializer_list<const char*> sources) {
    uint64_t key = ShaderCache::kHashSeed;
    key = ShaderCache::Hash(key, reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    key = ShaderCache::Hash(key, reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    key = ShaderCache::Hash(key, reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    for (const char* source : sources)
        key = ShaderCache::Hash(key, source);
    return key;
}


GLuint Shader::LoadProgramBinary(const String& shaderFolder, const String& fileName, uint64_t key) {
    std::vector<uint8_t> binary;
    uint32_t format = 0;
    if (not ShaderCache::Read(shaderFolder, fileName, key, binary, format))
        return 0;
    if (not HaveProgramBinaryFormat(GLenum(format)))
        return 0;
    GLuint handle = glCreateProgram();
    if (not handle)
        return 0;
    glProgramBinary(handle, GLenum(format), binary.data(), GLsizei(binary.size()));
    GLint isLinked = 0;
    glGetProgramiv(handle, GL_LINK_STATUS, &isLinked);
    if (isLinked == GL_TRUE)
        return handle;
    glDeleteProgram(handle);
    return 0;
}


void Shader::SaveProgramBinary(const String& shaderFolder, const String& fileName, uint64_t key) {
    GLint formatCount = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formatCount);
    if (formatCount <= 0)
        return;
    GLint length = 0;
    glGetProgramiv(m_handle, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0)
        return;
    std::vector<uint8_t> binary(size_t(length), 0);
    GLsizei written = 0;
    GLenum format = 0;
    glGetProgramBinary(m_handle, length, &written, &format, binary.data());
    if (written <= 0)
        return;
    ShaderCache::Write(shaderFolder, fileName, key, uint32_t(format), binary.data(), size_t(written));
}


bool Shader::Create(const String& vsCode, const String& fsCode, const String& gsCode, const String& tcsCode, const String& tesCode, const String& shaderFolder) {
    const bool useCache = not shaderFolder.IsEmpty();
    const String fileName = m_name + String(".glprog");
    const uint64_t key = useCache
                       ? ProgramKey({ static_cast<const char*>(vsCode), static_cast<const char*>(fsCode), static_cast<const char*>(gsCode), static_cast<const char*>(tcsCode), static_cast<const char*>(tesCode) })
                       : 0;
    if (useCache) {
        m_handle = LoadProgramBinary(shaderFolder, fileName, key);
        if (m_handle) {
            m_isTessellated = not tcsCode.IsEmpty();
            return true;
        }
    }
    m_handle = Link(Compile(static_cast<const char*>(vsCode), GL_VERTEX_SHADER), Compile(static_cast<const char*>(fsCode), GL_FRAGMENT_SHADER), Compile(static_cast<const char*>(gsCode), GL_GEOMETRY_SHADER),
                    Compile(static_cast<const char*>(tcsCode), GL_TESS_CONTROL_SHADER), Compile(static_cast<const char*>(tesCode), GL_TESS_EVALUATION_SHADER));
    if (m_handle == 0)
        return false;
    if (useCache)
        SaveProgramBinary(shaderFolder, fileName, key);
    return true;
}

// set modelview, projection and viewport matrices in shader. 
// Every shader program should have at least modelview and projection matrices.
// Also starts location indexing by calling m_locations.Start().
bool Shader::UpdateMatrices(void) {
    SetMatrix4f(bmModelView, baseRenderer.ModelView().AsArray(), false);
    SetMatrix4f(bmProjection, baseRenderer.Projection().AsArray(), false);
    SetMatrix4f(bmViewport, baseRenderer.ViewportTransformation().AsArray(), false);
    if (shadowMap.IsReady())
        SetMatrix4f(bmLightTransform, shadowMap.GetTransformation().AsArray(), false);
    return true;
}


static const char* baseMatrixNames[bmCount] = { "mModelView", "mProjection", "mViewport", "mLightTransform" };

GLint Shader::SetMatrix4f(eBaseMatrices id, const float* data, bool transpose) noexcept {
    GLint* location = m_baseLocations + id;
#if CACHE_SHADER_DATA
    if (UpdateUniform<const float*, UniformArray16f>(baseMatrixNames[id], location, data))
        glUniformMatrix4fv(*location, 1, GLboolean(transpose), data);
#else
    if (*location < -1)
        *location = GetLocation(baseMatrixNames[id]);
    if (*location >= 0)
        glUniformMatrix4fv(*location, 1, GLboolean(transpose), data);
#endif
    return *location;
}


GLint Shader::SetMatrix4f(const char* name, const float* data, bool transpose) noexcept {
#if CACHE_SHADER_DATA
    GLint* location = m_locations[name]; // .Current();
    if (UpdateUniform<const float*, UniformArray16f>(name, location, data))
        glUniformMatrix4fv(*location, 1, GLboolean(transpose), data);
    return *location;
#else
    GLint location = GetLocation(name);
    if (location >= 0)
        glUniformMatrix4fv(location, 1, GLboolean(transpose), data);
    return location;
#endif
}


GLint Shader::SetMatrix3f(const char* name, float* data, bool transpose) noexcept {
#if CACHE_SHADER_DATA
    GLint* location = m_locations[name]; // .Current();
-    if (UpdateUniform<float*, UniformArray9f>(name, location, data))
        glUniformMatrix3fv(*location, 1, GLboolean(transpose), data);
    return *location;
#else
    GLint location = GetLocation(name);
    if (location >= 0)
        glUniformMatrix3fv(location, 1, GLboolean(transpose), data);
    return location;
#endif
}

// =================================================================================================
