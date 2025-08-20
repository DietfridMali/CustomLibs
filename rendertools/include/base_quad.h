#pragma once

#include "vector.hpp"
#include "texcoord.h"
#include "shader.h"
#include "vao.h"
#include "vertexdatabuffers.h"
#include "texturehandler.h"
#include "colordata.h"
#include "plane.h"

// =================================================================================================

class BaseQuad 
    : public Plane
{
    public:
        static VAO*     m_vao;

        VertexBuffer    m_vertexBuffer;
        TexCoordBuffer  m_texCoordBuffer;
        TexCoord        m_maxTexCoord;
        Texture*        m_texture;
        RGBAColor       m_color;
        float           m_aspectRatio;
        float           m_offset;
        bool            m_isAvailable;

    BaseQuad() 
        : m_texture (nullptr), m_color(ColorData::White), m_aspectRatio(1), m_offset(0), m_isAvailable(false)
    { }


    BaseQuad(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords = {}, Texture* texture = nullptr, RGBAColor color = ColorData::White)
        : Plane(vertices), m_texture(texture), m_color(color), /*m_borderWidth(borderWidth),*/ m_isAvailable(true), m_offset(0)
    {
        Setup(vertices, texCoords, texture, color/*, borderWidth*/);
    }


    BaseQuad(const BaseQuad& other) {
		Copy(other);
    }

    bool Setup(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords = {}, Texture* texture = nullptr, RGBAColor color = ColorData::White);

    bool CreateVAO(void);

    BaseQuad& Copy(const BaseQuad& other);

    BaseQuad& Move(BaseQuad& other);

    BaseQuad& operator= (const BaseQuad& other) {
		return Copy(other);
    }

    BaseQuad operator= (BaseQuad&& other) noexcept {
        return Move(other);
    }

    void Destroy(void);

    void CreateTexCoords(void);

    bool UpdateVAO(void);

    float ComputeAspectRatio(void);

    inline void SetTexture(Texture* texture) {
        m_texture = texture;
    }

    void SetColor(RGBAColor color) {
        m_color = color;
    }

    Shader* LoadShader(bool useTexture, const RGBAColor& color = ColorData::White);

    void Render(RGBAColor color = ColorData::White);

    void Render(Shader* shader, Texture* texture, bool updateVAO = true);

    void Render(Texture* texture);

    // fill 2D area defined by x and y components of vertices with color color
    void Fill(const RGBAColor& color);

    void Fill(RGBAColor&& color) {
        Fill(static_cast<const RGBAColor&>(color));
    }

    inline void Fill(RGBColor color, float alpha = 1.0f) {
        return Fill(RGBAColor (color, alpha));
    }


    ~BaseQuad() {
        Destroy();
    }
};

// =================================================================================================
