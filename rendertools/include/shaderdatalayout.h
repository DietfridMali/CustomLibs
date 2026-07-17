#pragma once

#include "array.hpp"

#include <cstdint>
#include <cstring>

// =================================================================================================
// Central vertex attribute registry: maps a GfxDataBuffer's (datatype, id) tag to its fixed,
// engine-wide attribute slot and HLSL semantic. The slot is used as
//   - OpenGL: attribute location (GLSL: layout(location = N))
//   - Vulkan: VkVertexInputAttributeDescription location and binding (HLSL: [[vk::location(N)]])
//   - DX12:   input assembler slot (attribute matching itself runs on the HLSL semantics)
// so shaders address buffers by slot, independently of the order in which a mesh's buffers
// were created.
//   slot 0: Vertex, slot 1-3: TexCoord/0-2, slot 4: Color, slot 5: Normal, slot 6: Tangent,
//   slot 7-10: Offset/0-3, slot 11-12: Float/0-1
// 13 slots total - OpenGL and Vulkan only guarantee 16 vertex attributes/bindings, so any
// extension of this table must stay below that limit.
// Returns -1 for unknown (datatype, id) tags; callers skip such buffers.

inline int GfxAttributeSlot(const char* datatype, int id) noexcept
{
    if (strcmp(datatype, "Vertex") == 0)
        return 0;
    if (strcmp(datatype, "TexCoord") == 0)
        return ((id >= 0) and (id <= 2)) ? 1 + id : -1;
    if (strcmp(datatype, "Color") == 0)
        return 4;
    if (strcmp(datatype, "Normal") == 0)
        return 5;
    if (strcmp(datatype, "Tangent") == 0)
        return 6;
    if (strcmp(datatype, "Offset") == 0)
        return ((id >= 0) and (id <= 3)) ? 7 + id : -1;
    if (strcmp(datatype, "Float") == 0)
        return ((id >= 0) and (id <= 1)) ? 11 + id : -1;
    return -1;
}

// Maps (datatype, id) to an HLSL semantic name and index.
inline const char* GfxAttributeSemantic(const char* datatype, int id, uint32_t& semanticIndex) noexcept
{
    if (strcmp(datatype, "Vertex") == 0) {
        semanticIndex = 0;
        return "POSITION";
    }
    if (strcmp(datatype, "TexCoord") == 0) {
        semanticIndex = uint32_t(id);
        return "TEXCOORD";
    }
    if (strcmp(datatype, "Color") == 0) {
        semanticIndex = uint32_t(id);
        return "COLOR";
    }
    if (strcmp(datatype, "Normal") == 0) {
        semanticIndex = uint32_t(id);
        return "NORMAL";
    }
    if (strcmp(datatype, "Tangent") == 0) {
        semanticIndex = uint32_t(id);
        return "TANGENT";
    }
    if (strcmp(datatype, "Offset") == 0) {
        semanticIndex = uint32_t(id);
        return "OFFSET";
    }
    if (strcmp(datatype, "Float") == 0) {
        semanticIndex = uint32_t(id);
        return "FLOAT";
    }
    semanticIndex = 0;
    return "TEXCOORD";
}

// =================================================================================================

enum class TextureFormat {
    None,
    R8, RG8, RGBA8,
    R16F, RG16F, RGBA16F,
    R32F, RG32F, RGBA32F,
    D24S8
};

// =================================================================================================
// API-neutral description of vertex data streams fed into a shader.
// Used by ShaderSource to declare expected inputs; interpreted by the DX12 backend
// to build per-shader D3D12_INPUT_ELEMENT_DESC arrays. Ignored by the OpenGL backend.

struct ShaderDataAttributes {
    const char* datatype;   // C++ buffer type: "Vertex", "Normal", "Color",
                            //   "TexCoord", "Tangent", "Offset"
    int         id;         // index for multi-instance types (TexCoord/0, Offset/2, ...)
    enum Format { Float1, Float2, Float3, Float4 } format;
};

static constexpr int MaxRenderTargets = 8;

class ShaderDataLayout {
public:
    const ShaderDataAttributes* m_attrs{ nullptr };
    int                         m_count{ 0 };
    int                         m_numRenderTargets{ 1 };
    StaticArray<TextureFormat, MaxRenderTargets> m_rtvFormats{ TextureFormat::RGBA8 };

    ShaderDataLayout() = default;

    ShaderDataLayout(const ShaderDataAttributes* attrs, int count, int numRenderTargets = 1)
        : m_attrs(attrs)
        , m_count(count)
        , m_numRenderTargets(numRenderTargets)
    { }

    ShaderDataLayout& SetFormats(std::initializer_list<TextureFormat> formats) {
        int i = 0;
        for (auto fmt : formats)
            if (i < MaxRenderTargets)
                m_rtvFormats[i++] = fmt;
        return *this;
    }
};

// =================================================================================================
