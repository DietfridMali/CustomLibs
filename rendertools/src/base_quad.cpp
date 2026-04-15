
#define NOMINMAX
#include <algorithm>

#include "gfxDataBuffer.h"
#include "gfxDataLayout.h"
#include "base_quad.h"
#include "base_shaderhandler.h"
#include "type_helper.hpp"
#include "base_renderer.h"
#include "conversions.hpp"
#include "tristate.h"

// caution: the GfxDataLayout shared handle needs glGenVertexArrays and glDeleteVertexArrays, which usually are not yet available when this gfxDataLayout is initialized.
// GfxDataLayout::Init takes care of that by first assigning a handle-less shared gl handle 
// GfxDataLayout* BaseQuad::m_gfxDataLayout = nullptr;

#if USE_STATIC_GFX_DATA 
GfxDataLayout BaseQuad::staticGfxDataLayout;
#endif

// =================================================================================================

std::initializer_list<Vector3f> BaseQuad::defaultVertices[3] = {
    { Vector3f{-0.5f, -0.5f, 0.0f}, Vector3f{-0.5f, 0.5f, 0.0f}, Vector3f{0.5f, 0.5f, 0.0f}, Vector3f{0.5f, -0.5f, 0.0f} },
    { Vector3f{0.0f, 0.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, Vector3f{1.0f, 1.0f, 0.0f}, Vector3f{1.0f, 0.0f, 0.0f} },
    { Vector3f{-0.5f, -0.5f, -1.0f}, Vector3f{-0.5f, 0.5f, -1.0f}, Vector3f{0.5f, 0.5f, -1.0f}, Vector3f{0.5f, -0.5f, -1.0f} }
};

std::initializer_list<TexCoord> BaseQuad::defaultTexCoords[6] = {
#if 1
    { TexCoord{0, 1}, TexCoord{0, 0}, TexCoord{1, 0}, TexCoord{1, 1} }, // regular
    { TexCoord{0, 0}, TexCoord{0, 1}, TexCoord{1, 1}, TexCoord{1, 0} }, // v flip
    { TexCoord{1, 1}, TexCoord{1, 0}, TexCoord{0, 0}, TexCoord{0, 1} }, // h flip
    { TexCoord{1, 0}, TexCoord{1, 1}, TexCoord{0, 1}, TexCoord{0, 0} }, // v + h flip
    { TexCoord{0, 0}, TexCoord{1, 0}, TexCoord{1, 1}, TexCoord{0, 1} }, // rotate left (ccw) 90 deg
    { TexCoord{1, 1}, TexCoord{0, 1}, TexCoord{0, 0}, TexCoord{1, 0} }, // rotate right (cw) 90 deg
#else
    { TexCoord{0, 1}, TexCoord{0, 0}, TexCoord{1, 0}, TexCoord{1, 1} }, // regular
    { TexCoord{0, 0}, TexCoord{0, 1}, TexCoord{1, 1}, TexCoord{1, 0} }, // v flip
    { TexCoord{1, 1}, TexCoord{1, 0}, TexCoord{0, 0}, TexCoord{0, 1} }, // h flip
    { TexCoord{1, 0}, TexCoord{1, 1}, TexCoord{0, 1}, TexCoord{0, 0} }, // v + h flip
    { TexCoord{0, 0}, TexCoord{1, 0}, TexCoord{1, 1}, TexCoord{0, 1} }, // rotate left (ccw) 90 deg
    { TexCoord{1, 1}, TexCoord{0, 1}, TexCoord{0, 0}, TexCoord{1, 0} }, // rotate right (cw) 90 deg
#endif
};

// =================================================================================================

void BaseQuad::Init(void) {
    // just create the gfxDataLayout and its GfxDataBuffers
    Setup(defaultVertices[0], defaultTexCoords[0]);
}


BaseQuad& BaseQuad::Copy(const BaseQuad& other) {
    if (this != &other) {
        if (m_privateGfxData)
            m_gfxDataLayout->Copy(*other.m_gfxDataLayout);
        m_vertices = other.m_vertices;
        m_texCoords[0] = other.m_texCoords[0];
        m_aspectRatio = other.m_aspectRatio;
        m_isAvailable = other.m_isAvailable;
    }
    return *this;
}


BaseQuad& BaseQuad::Move(BaseQuad& other)
noexcept
{
    if (this != &other) {
        m_gfxDataLayout = other.m_gfxDataLayout;
        if ((m_privateGfxData = other.m_privateGfxData))
            other.m_gfxDataLayout = nullptr;
        m_vertices = std::move(other.m_vertices);
        m_texCoords[0] = std::move(other.m_texCoords[0]);
        m_aspectRatio = other.m_aspectRatio;
        m_isAvailable = other.m_isAvailable;
    }
    return *this;
}


void BaseQuad::UpdateTexCoords(void) {
    if (m_texCoords[0].AppDataLength() > 0) {
        for (auto& tc : m_texCoords[0].AppData())
            m_maxTexCoord = TexCoord({ std::max(m_maxTexCoord.U(), tc.U()), std::max(m_maxTexCoord.V(), tc.V()) });
    }
}


bool BaseQuad::Setup(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords, bool privateGfxData) {

    auto equals = [](auto const& c, std::initializer_list<typename std::decay_t<decltype(*c.begin())>> il) {
        return c.size() == il.size() && std::equal(c.begin(), c.end(), il.begin());
        };

    if (vertices.size() and not equals(m_vertices.AppData().StdList(), vertices)) {
        CoplanarRectangle::Init(vertices);
        m_vertices.AppData() = vertices;
        m_vertices.SetDirty(true);
    }

    if (texCoords.size() == 0)
        texCoords = defaultTexCoords[tcRegular];
    if (not equals(m_texCoords[0].AppData().StdList(), texCoords)) {
        m_texCoords[0].AppData() = texCoords;
        //m_texCoords.Setup();
        m_texCoords[0].SetDirty(true);
    }
    UpdateTexCoords();

    if (not CreateLayout())
        return false;
    //SetShape(GL_TRIANGLES); // trigger building of triangle index everytime new vertices are loaded
    UpdateData();
    //SetShape(GL_QUADS); // trigger building of triangle index everytime new vertices are loaded
    m_aspectRatio = ComputeAspectRatio();
    return true;
}


float BaseQuad::ComputeAspectRatio(void)
noexcept
{
    Vector3f vMin = Vector3f{ 1e6, 1e6, 1e6 };
    Vector3f vMax = Vector3f{ -1e6, -1e6, -1e6 };
    for (auto& v : m_vertices.AppData()) {
        vMin.Minimize(v);
        vMax.Maximize(v);
    }
    return (vMax.Y() - vMin.Y()) / (vMax.X() - vMin.X());
}


Shader* BaseQuad::LoadShader(bool useTexture, const RGBAColor& color) {
    UpdateTransformation();
    return
        useTexture 
        ? baseShaderHandler.LoadPlainTextureShader(color, Vector2f::ZERO, Vector2f::ONE, m_premultiply) 
        : baseShaderHandler.LoadPlainColorShader(color, m_premultiply);
}


void BaseQuad::UpdateTransformation(void) {
    if (HaveTransformations()) {
        baseRenderer.PushMatrix();
        if (m_transformations.centerOrigin)
            baseRenderer.Translate(0.5f, 0.5f, 0.0f);
        if (m_transformations.rotation != 0.0f)
            baseRenderer.Rotate(m_transformations.rotation, 0, 0, 1);
        if (m_transformations.flipVertically)
            baseRenderer.Scale(1.0f, -1.0f, 1.0f);
    }
}


void BaseQuad::ResetTransformation(void) {
    if (HaveTransformations()) {
        baseRenderer.PopMatrix();
        if (m_transformations.autoClear)
            ClearTransformations();
    }
}


bool BaseQuad::Render(Shader* shader, std::span<Texture* const> textures, const RGBAColor& color) {
    if (not (shader or (shader = LoadShader(textures.size() != 0, color))))
        return false;
    if (UpdateData()) {
        m_gfxDataLayout->Render(textures);
        ResetTransformation();
        return true;
    }
    return false;
}


// fill 2D area defined by x and y components of vertices with color color
bool BaseQuad::Fill(const RGBAColor& color) {
    return Render(nullptr, {}, color);
}

// =================================================================================================
