#pragma once

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

class ShaderDataLayout {
public:
    const ShaderDataAttributes* m_attrs{ nullptr };
    int                         m_count{ 0 };

    ShaderDataLayout() = default;

    ShaderDataLayout(const ShaderDataAttributes* attrs, int count)
        : m_attrs(attrs)
        , m_count(count)
    { }
};

// =================================================================================================
