#pragma once

#include "array.hpp"

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
