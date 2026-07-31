#pragma once

#include "vector.hpp"
#include "texcoord.h"
#include "shader.h"
#include "gfxdatalayout.h"
#include "vertexdatabuffers.h"
#include "texturehandler.h"
#include "colordata.h"
#include "quad.h"
#include "mesh.h"

#define USE_STATIC_GFX_DATA 0

// =================================================================================================

class BaseQuadMesh
    : public Quad
    , public Mesh
{
public:
    //static GfxDataLayout* m_gfxDataLayout;

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

#if USE_STATIC_GFX_DATA 
    static GfxDataLayout   staticGfxData;
#endif
#if 0
    GfxDataLayout*          m_gfxDataLayout;
    VertexBuffer            m_vertexBuffer;
    TexCoordBuffer          m_texCoordBuffer;
#endif
    TexCoord                m_maxTexCoord;
    float                   m_aspectRatio;
    float                   m_offset;
    bool                    m_isAvailable;
    bool                    m_privateGfxData;
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

    static std::initializer_list<Vector3f> defaultVertices[3];
    static std::initializer_list<TexCoord> defaultTexCoords[6];

    BaseQuadMesh()
        : m_aspectRatio(1.0f)
        , m_offset(0.0f)
        , m_isAvailable(false)
        , m_privateGfxData(false)
        , m_premultiply(false)
    {
        Mesh::Init(MeshTopology::Quads, 100);
    }

#pragma warning(push)
#pragma warning(disable:4100)
    BaseQuadMesh(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords = defaultTexCoords[tcRegular], bool privateGfxData = false)
#pragma warning(pop)
        : Quad(vertices)
        , m_isAvailable(true)
        , m_privateGfxData(privateGfxData)
        , m_premultiply(false)
    {
        Mesh::Init(MeshTopology::Quads, 100);
        Setup(vertices, texCoords);
    }

    BaseQuadMesh(const BaseQuadMesh& other) {
        Copy(other);
    }

    ~BaseQuadMesh() = default;

    void Init(void);

    virtual bool Setup(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords = defaultTexCoords[tcRegular], bool privateGfxData = false);

    BaseQuadMesh& Copy(const BaseQuadMesh& other);

    BaseQuadMesh& Move(BaseQuadMesh& other)
 noexcept;

    BaseQuadMesh& operator= (const BaseQuadMesh& other) {
        return Copy(other);
    }

    BaseQuadMesh& operator= (BaseQuadMesh&& other) noexcept {
        return Move(other);
    }

    inline GfxDataLayout& GetGfxDataLayout(void) {
        return *m_gfxDataLayout;
    }

    float ComputeAspectRatio(void)
 noexcept;

    Shader* LoadShader(std::span<Texture* const> textures, const RGBAColor& color = ColorData::White);

    bool Render(Shader* shader, std::span<Texture* const> textures = {}, const RGBAColor& color = ColorData::White);

    inline bool Render(Shader* shader, std::initializer_list<Texture*> textures) {
        return Render(shader, std::span<Texture* const>(textures.begin(), textures.size()));
    }

    inline bool Render(Shader* shader, Texture* texture) {
        return Render(shader, texture ? std::span<Texture* const>(&texture, 1) : std::span<Texture* const>{});
    }

    inline bool Render(Shader* shader, Texture* texture, const RGBAColor& color) {
        return Render(shader, texture ? std::span<Texture* const>(&texture, 1) : std::span<Texture* const>{}, color);
    }

    inline bool Render(std::span<Texture* const> textures = {}) {
        return Render(nullptr, textures);
    }
#if 0
    inline bool Render(Shader* shader, Texture* texture, const RGBAColor& color) {
        return Render(shader, texture ? { texture } : {}, color);
    }

    inline bool Render(Texture* texture = nullptr) {
        return Render(nullptr, texture ? { texture } : {});
    }
#endif
    inline bool Render(RGBAColor color = ColorData::White) {
        return Render(nullptr, {}, static_cast<const RGBAColor&>(color));
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

    protected:
        void UpdateTexCoords(void);

        void UpdateTransformation(void);

        void ResetTransformation(void);

        inline bool HaveTransformations(void) noexcept {
            return m_transformations.HaveTransformations();
        }
};

// =================================================================================================
