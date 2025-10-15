#pragma once

#include "vector.hpp"
#include "texcoord.h"
#include "shader.h"
#include "vao.h"
#include "vertexdatabuffers.h"
#include "texturehandler.h"
#include "colordata.h"
#include "plane.h"
#include "mesh.h"

#define USE_STATIC_VAO 0

// =================================================================================================

class BaseQuad
    : public Plane
    , public Mesh
{
public:
    //static VAO* m_vao;

    struct TransformationParams {
        bool            centerOrigin{ false };
        bool            flipVertically{ false };
        float           rotation{ 0.0f };
        bool            autoClear{ true };

        bool HaveTransformations(void) { 
            return centerOrigin or flipVertically or (rotation != 0.0f); 
        }
        void Clear(void) {  *this = {}; }
    };

    //static inline constexpr TransformationParams defaultTransformationParams = TransformationParams{};

#if USE_STATIC_VAO 
    static VAO              staticVAO;
#endif
#if 0
    VAO*                    m_vao;
    VertexBuffer            m_vertexBuffer;
    TexCoordBuffer          m_texCoordBuffer;
#endif
    TexCoord                m_maxTexCoord;
    float                   m_aspectRatio;
    float                   m_offset;
    bool                    m_isAvailable;
    bool                    m_privateVAO;
    bool                    m_premultiply;
    TransformationParams    m_transformations;

    typedef enum {
        voCenter,
        voZero
    } eVertexOrigins;

    typedef enum {
        tcRegular,
        tcFlipVert, // flip vertically
        tcFlipHorz, // flip horizontally
        tcFlipBoth, // flip in both directions
        tcRotLeft90,
        tcRotRight90
    } eTexCoordTransforms;

    static std::initializer_list<Vector3f> defaultVertices[2];
    static std::initializer_list<TexCoord> defaultTexCoords[6];

    BaseQuad()
        : m_aspectRatio(1.0f)
        , m_offset(0.0f)
        , m_isAvailable(false)
        , m_privateVAO(false)
        , m_premultiply(false)
    {
        Mesh::Init(GL_QUADS, 100);
    }

    BaseQuad(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords = defaultTexCoords[tcRegular], bool privateVAO = false)
        : Plane(vertices)
        , m_isAvailable(true)
        , m_premultiply(false)
    {
        Mesh::Init(GL_QUADS, 100);
        Setup(vertices, texCoords);
    }

    BaseQuad(const BaseQuad& other) {
        Copy(other);
    }

    virtual bool Setup(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords = defaultTexCoords[tcRegular], bool privateVAO = false);

    BaseQuad& Copy(const BaseQuad& other);

    BaseQuad& Move(BaseQuad& other)
        noexcept;

    BaseQuad& operator= (const BaseQuad& other) {
        return Copy(other);
    }

    BaseQuad& operator= (BaseQuad&& other) noexcept {
        return Move(other);
    }

    void Destroy(void)
        noexcept;

    inline VAO& GetVAO(void) {
        return *m_vao;
    }

    float ComputeAspectRatio(void)
        noexcept;

    Shader* LoadShader(bool useTexture, const RGBAColor& color = ColorData::White);

    bool Render(Shader* shader, Texture* texture = nullptr, const RGBAColor& color = ColorData::White);

    inline bool Render(Shader* shader, Texture* texture, RGBAColor&& color) {
        return Render(shader, texture, static_cast<const RGBAColor&>(color));
    }

    inline bool Render(Texture* texture) {
        return Render(nullptr, texture);
    }

    inline bool Render(RGBAColor color = ColorData::White) {
        return Render(nullptr, nullptr, static_cast<const RGBAColor&>(color));
    }

    // fill 2D area defined by x and y components of vertices with color color
    bool Fill(const RGBAColor& color);

    bool Fill(RGBAColor&& color) {
        return Fill(static_cast<const RGBAColor&>(color));
    }

    inline bool Fill(RGBColor color, float alpha = 1.0f) {
        return Fill(RGBAColor(color, alpha));
    }

    inline void SetTransformations(const TransformationParams& params) {
        m_transformations = params;
    }

    inline void ClearTransformations(void) {
        m_transformations.Clear();
    }

    inline void Premultiply(void) {
        m_premultiply = true;
    }

    ~BaseQuad() {
        if (not m_privateVAO)
            m_vao = nullptr;
        //Destroy();
    }

    protected:
        bool CreateVAO(bool privateVAO);

        void UpdateTexCoords(void);

        void UpdateTransformation(void);

        void ResetTransformation(void);

        inline bool HaveTransformations(void) noexcept {
            return m_transformations.HaveTransformations();
        }
};

// =================================================================================================
