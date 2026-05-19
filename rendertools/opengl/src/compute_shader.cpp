
#include "compute_shader.h"
#include "texture.h"
#include "rendertarget.h"

// =================================================================================================
// OpenGL ComputeShader — see compute_shader.h for the binding model and rationale.

// -------------------------------------------------------------------------------------------------

bool ComputeShader::Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings) {
    m_cs = csCode;
    m_bindings = bindings;

    GLuint csHandle = Compile((const char*)m_cs, GL_COMPUTE_SHADER);
    if (not csHandle)
        return false;

    GLuint program = glCreateProgram();
    if (not program) {
        glDeleteShader(csHandle);
        return false;
    }
    glAttachShader(program, csHandle);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
#ifdef _DEBUG
        String infoLog = GetInfoLog(program, true);
        fprintf(stderr, "\n***** GLSL linker error in %s compute shader: *****\n\n", (char*)m_name);
        PrintShaderSource(csHandle, String("Compute shader:"));
#endif
        glDeleteShader(csHandle);
        glDeleteProgram(program);
        return false;
    }
    glDetachShader(program, csHandle);
    glDeleteShader(csHandle);
    m_handle = program;
    return true;
}


bool ComputeShader::Activate(void) {
    if (not IsValid())
        return false;
    glUseProgram(m_handle);
    return true;
}


bool ComputeShader::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (not IsValid())
        return false;
    glDispatchCompute(groupCountX, groupCountY, groupCountZ);
    return true;
}


bool ComputeShader::Dispatch(std::span<Texture* const> textures,
                             uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (not IsValid())
        return false;
    int unit = 0;
    for (Texture* tex : textures) {
        if (tex != nullptr)
            tex->Bind(unit);
        ++unit;
    }
    glDispatchCompute(groupCountX, groupCountY, groupCountZ);
    return true;
}


bool ComputeShader::Dispatch2D(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY) {
    if (tileX == 0 or tileY == 0)
        return false;
    uint32_t gx = (width + tileX - 1) / tileX;
    uint32_t gy = (height + tileY - 1) / tileY;
    return Dispatch(gx, gy, 1);
}


bool ComputeShader::BindSampledImage(uint32_t unit, Texture* texture) {
    if (not texture)
        return false;
    return texture->Bind(int(unit));
}


bool ComputeShader::BindStorageImage(uint32_t unit, RenderTarget* target, int bufferIndex,
                                     GLenum access, GLenum internalFormat, int level) {
    if (not target)
        return false;
    GLuint texHandle = target->GetHandle(bufferIndex);
    if (not texHandle)
        return false;
    glBindImageTexture(unit, texHandle, level, GL_FALSE, 0, access, internalFormat);
    return true;
}

// =================================================================================================
