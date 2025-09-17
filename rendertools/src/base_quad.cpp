
#define NOMINMAX
#include <algorithm>

#include "vbo.h"
#include "vao.h"
#include "base_quad.h"
#include "base_shaderhandler.h"
#include "type_helper.hpp"
#include "base_renderer.h"
#include "conversions.hpp"

// caution: the VAO shared handle needs glGenVertexArrays and glDeleteVertexArrays, which usually are not yet available when this vao is initialized.
// VAO::Init takes care of that by first assigning a handle-less shared gl handle 
// VAO* BaseQuad::m_vao = nullptr;

#if USE_STATIC_VAO 
VAO BaseQuad::staticVAO;
#endif

// =================================================================================================

std::initializer_list<Vector3f> BaseQuad::defaultVertices[2] = {
    { Vector3f{-0.5f, -0.5f, 0.0f}, Vector3f{-0.5f, 0.5f, 0.0f}, Vector3f{0.5f, 0.5f, 0.0f}, Vector3f{0.5f, -0.5f, 0.0f} },
    { Vector3f{0.0f, 0.0f, 0.0f}, Vector3f{0.0f, 1.0f, 0.0f}, Vector3f{1.0f, 1.0f, 0.0f}, Vector3f{1.0f, 0.0f, 0.0f} }
};

std::initializer_list<TexCoord> BaseQuad::defaultTexCoords[6] = {
    { TexCoord{0, 1}, TexCoord{0, 0}, TexCoord{1, 0}, TexCoord{1, 1} }, // regular
    { TexCoord{0, 0}, TexCoord{0, 1}, TexCoord{1, 1}, TexCoord{1, 0} }, // v flip
    { TexCoord{1, 1}, TexCoord{1, 0}, TexCoord{0, 0}, TexCoord{0, 1} }, // h flip
    { TexCoord{1, 0}, TexCoord{1, 1}, TexCoord{0, 1}, TexCoord{0, 0} }, // v + h flip
    { TexCoord{0, 0}, TexCoord{1, 0}, TexCoord{1, 1}, TexCoord{0, 1} }, // rotate left (ccw) 90 deg
    { TexCoord{1, 1}, TexCoord{0, 1}, TexCoord{0, 0}, TexCoord{1, 0} }, // rotate right (cw) 90 deg
};

// =================================================================================================

BaseQuad& BaseQuad::Copy(const BaseQuad& other) {
    if (this != &other) {
        if (m_privateVAO)
            m_vao->Copy(*other.m_vao);
        m_vertexBuffer = other.m_vertexBuffer;
        m_texCoordBuffer = other.m_texCoordBuffer;
        m_aspectRatio = other.m_aspectRatio;
        m_isAvailable = other.m_isAvailable;
    }
    return *this;
}


BaseQuad& BaseQuad::Move(BaseQuad& other)
noexcept
{
    if (this != &other) {
        m_vao = other.m_vao;
        if ((m_privateVAO = other.m_privateVAO))
            other.m_vao = nullptr;
        m_vertexBuffer = std::move(other.m_vertexBuffer);
        m_texCoordBuffer = std::move(other.m_texCoordBuffer);
        m_aspectRatio = other.m_aspectRatio;
        m_isAvailable = other.m_isAvailable;
    }
    return *this;
}


void BaseQuad::CreateTexCoords(void) {
    if (m_texCoordBuffer.AppDataLength() > 0) {
        for (auto& tc : m_texCoordBuffer.AppData())
            m_maxTexCoord = TexCoord({ std::max(m_maxTexCoord.U(), tc.U()), std::max(m_maxTexCoord.V(), tc.V()) });
    }
}


bool BaseQuad::Setup(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords, bool privateVAO) {

    auto equals = [](auto const& c, std::initializer_list<typename std::decay_t<decltype(*c.begin())>> il) {
        return c.size() == il.size() && std::equal(c.begin(), c.end(), il.begin());
        };

    if (vertices.size() and not equals(m_vertexBuffer.AppData().StdList(), vertices)) {
        Plane::Init(vertices);
        m_vertexBuffer.AppData() = vertices;
        m_vertexBuffer.Setup();
        m_vertexBuffer.SetDirty(true);
    }

    if (texCoords.size() == 0)
        texCoords = defaultTexCoords[tcRegular];
    if (not equals(m_texCoordBuffer.AppData().StdList(), texCoords)) {
        m_texCoordBuffer.AppData() = texCoords;
        m_texCoordBuffer.Setup();
        m_texCoordBuffer.SetDirty(true);
    }
    CreateTexCoords();

    if (not CreateVAO(privateVAO))
        return false;
    UpdateVAO();
    m_aspectRatio = ComputeAspectRatio();
    return true;
}


bool BaseQuad::CreateVAO(bool privateVAO) {
#if USE_STATIC_VAO 
    if (privateVAO and not m_vao) {
        m_vao = new VAO();
        m_privateVAO = m_vao != nullptr;
    }
    if (not m_privateVAO) // fallback
        m_vao = &staticVAO;
    return m_vao->Create(GL_QUADS, true);
#else
    if (m_vao)
        return true;
    m_vao = new VAO();
    if (not m_vao)
        return false;
    return m_vao->Create(GL_QUADS, true);
#endif
}


bool BaseQuad::UpdateVAO(void) {
    if (not m_vao->IsValid())
        return false;
    if (m_vertexBuffer.IsDirty() or m_texCoordBuffer.IsDirty()) {
        m_vao->Enable();
        m_vao->UpdateVertexBuffer("Vertex", m_vertexBuffer, GL_FLOAT);
        m_vao->UpdateVertexBuffer("TexCoord", m_texCoordBuffer, GL_FLOAT);
        m_vao->Disable();
    }
    return true;
}


float BaseQuad::ComputeAspectRatio(void)
noexcept
{
    Vector3f vMin = Vector3f{ 1e6, 1e6, 1e6 };
    Vector3f vMax = Vector3f{ -1e6, -1e6, -1e6 };
    for (auto& v : m_vertexBuffer.AppData()) {
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


bool BaseQuad::Render(Shader* shader, Texture* texture, const RGBAColor& color) {
    if (not (shader or (shader = LoadShader(texture != nullptr, color))))
        return false;
    if (UpdateVAO()) 
    {
        m_vao->Render(texture);
        ResetTransformation();
        return true;
        }

    if (baseRenderer.LegacyMode) {
        if (texture and texture->Enable()) {
            openGLStates.SetBlending(1);
            glBegin(GL_QUADS);
            for (auto& v : m_vertexBuffer.AppData()) {
                glColor4f(1, 1, 1, 1);
                glVertex3f(v.X(), v.Y(), v.Z());
            }
            glEnd();
            texture->Disable();
            openGLStates.SetBlending(0);
        }
    }
    return false;
}


// fill 2D area defined by x and y components of vertices with color color
bool BaseQuad::Fill(const RGBAColor& color) {
    if (Render(nullptr, nullptr, color))
        return true;
    if (not baseRenderer.LegacyMode)
        return false;
    //openGLStates.SetTexture2D(false);
    glBegin(GL_QUADS);
    glColor4f(color.R(), color.G(), color.B(), color.A());
    glVertex2f(0, 0);
    glVertex2f(0, 1);
    glVertex2f(1, 1);
    glVertex2f(1, 0);
    glEnd();
    return true;
}


void BaseQuad::Destroy(void)
noexcept
{
    if constexpr (not is_static_member_v<&BaseQuad::m_vao>) {
        if (m_privateVAO) {
            delete m_vao;
            m_vao = nullptr;
        }
    }
}

// =================================================================================================
