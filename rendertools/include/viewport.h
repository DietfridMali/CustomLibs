#pragma once

#include "rectangle.h"
#include "vector.hpp"
#include "matrix.hpp"
#include "base_quad.h"
#include "colordata.h"

// =================================================================================================

struct ScreenCoord {
    int x;
    int y;
};

class Viewport : public Rectangle
{
public:
    Matrix4f    m_transformation;
    int         m_windowWidth;
    int         m_windowHeight;
    Vector2f    m_center;
    bool        m_flipVertically;
    GLint       m_glViewport[4];

    Viewport(int left = 0, int top = 0, int width = 0, int height = 0)
        : Rectangle(left, top, width, height) 
        , m_windowWidth(0)
        , m_windowHeight(0)
        , m_center({ float(left) + float (width) * 0.5f, float(top) + float(height) * 0.5f })
        , m_flipVertically(false)
    { }

    Viewport(Vector2i center, int width = 0, int height = 0)
        : Rectangle(center.x - width / 2, center.y - height / 2, width, height)
        , m_windowWidth(0)
        , m_windowHeight(0)
        , m_center({ float(center.x), float(center.y) })
        , m_flipVertically(false)
    { }

    Viewport(Rectangle& r)
        : Rectangle (r.m_left, r.m_top, r.m_width, r.m_height) 
        , m_windowWidth(0)
        , m_windowHeight(0)
        , m_center({ float(r.m_left) + float(r.m_width) * 0.5f, float(r.m_top) + float(r.m_height) * 0.5f })
        , m_flipVertically(false)
    { }

    void Fill(const RGBColor& color, float alpha = 1.0f, float scale = 1.0f);

    inline void Fill(RGBColor&& color, float alpha = 1.0f, float scale = 1.0f) {
        Fill(static_cast<const RGBColor&>(color), alpha, scale);
    }

    inline void Fill(void) {
        Fill(static_cast<RGBColor>(ColorData::White));
    }

    inline float Leftf(void) noexcept { return float(m_left); }

    inline float Topf(void) noexcept { return float(m_top); }

    inline float Widthf(void) noexcept { return float(m_width); }

    inline float Heightf(void) noexcept { return float(m_height); }

    inline int WindowWidth(void) noexcept { return m_windowWidth; }

    inline int WindowHeight(void) noexcept { return m_windowHeight; }

    inline int FlipVertically(void) noexcept { return m_flipVertically; }

    inline Vector2f Center(void) const noexcept {
        return m_center;
    }

    inline Vector2i Centeri(void) const noexcept {
        return Vector2i(int(round(m_center.X())), int(round(m_center.Y())));
    }

    inline Matrix4f& Transformation(void) noexcept { return m_transformation; }

    void SetViewport(void);

    Viewport& Resize(float scale);

    Viewport Resized(int deltaLeft, int deltaTop, int deltaWidth, int deltaHeight) const;

    Viewport Resized(float scale) const;

    void SetResized(int deltaLeft, int deltaTop, int deltaWidth, int deltaHeight) const;

    void BuildTransformation(int windowWidth, int windowHeight, bool flipVertically) noexcept;

    Viewport& Move(int dx, int dy);

    Viewport Moved(int dx, int dy) {
        return Viewport(m_left + dx, m_top + dy, m_width, m_height);
    }


    Viewport Movedf(float dx, float dy) {
        return Viewport(m_left + int(m_width * dx), int (m_top + m_height * dy), m_width, m_height);
    }


    Viewport& operator+=(Vector2i offset) {
        m_left += offset.x;
        m_right += offset.x;
        m_top += offset.y;
        m_bottom += offset.y;
        m_center.X() += offset.x;
        m_center.Y() += offset.y;
        return *this;
    }


    Viewport& operator+=(Vector2f offset) {
        *this += Vector2i(int(m_width * offset.x), int(m_height * offset.y));
        return *this;
    }


    Viewport& operator*=(float scale) {
        if (fabs(scale) > Conversions::NumericTolerance) {
            float w = float(m_width) * scale;
            float h = float(m_height) * scale;
            if (w * h <= Conversions::NumericTolerance)
                return *this;
            m_left = int(round(m_center.X() - w * 0.5f));
            m_top = int(round(m_center.Y() - h * 0.5f));
            m_right = int(round(m_center.X() + w * 0.5f));
            m_bottom = int(round(m_center.Y() + h * 0.5f));
            m_width = int(round(w));
            m_height = int(round(h));
        }
        return *this;
    }

    inline void GetGlViewport(void) noexcept {
        glGetIntegerv(GL_VIEWPORT, m_glViewport);
    }

    inline void SetGlViewport(void) noexcept {
        glViewport(m_glViewport[0], m_glViewport[1], m_glViewport[2], m_glViewport[3]);
    }

};

// =================================================================================================
