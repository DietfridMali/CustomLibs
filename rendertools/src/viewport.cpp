#include <utility>
#include "glew.h"
#include "viewport.h"
#include "base_renderer.h"
#include "conversions.hpp"

// =================================================================================================

void Viewport::Fill(const RGBColor& color, float alpha, float scale) {
    SetViewport();
    baseRenderer.Fill(color, alpha, scale);
}


void Viewport::SetViewport(void) {
    baseRenderer.SetViewport(*this);
}


Viewport& Viewport::Move(int dx, int dy) {
    if (dx) {
        m_left += dx;
        m_right += dx;
        m_center.X() += dx;
    }
    if (dy) {
        m_top += dy;
        m_bottom += dy;
        m_center.Y() += dy;
    }
    return *this;
}


Viewport& Viewport::Resize(float scale) {
    if (scale == 1.0f)
        return *this;
    m_width = int(round(float(m_width) * scale));
    m_height = int(round(float(m_height) * scale));
    m_left = int(m_center.X()) - m_width / 2;
    m_top = int(m_center.Y()) - m_height / 2;
    m_right = m_left + m_width;
    m_bottom = m_top + m_height;
    m_center.X() = float(m_left) + float(m_width) * 0.5f;
    m_center.Y() = float(m_top) + float(m_height) * 0.5f;
    return *this;
}


Viewport Viewport::Resized(int deltaLeft, int deltaTop, int deltaWidth, int deltaHeight) const {
    return Viewport (m_left + deltaLeft, m_top + deltaTop, m_width + deltaWidth, m_height + deltaHeight);
}


Viewport Viewport::Resized(float scale) const {
    if (scale == 1.0f)
        return *this;
    float w = float(m_width) * scale;
    float h = float(m_height) * scale;
    if (w * h <= Conversions::NumericTolerance)
        return *this;
    return Viewport(int (round(m_center.X() - w * 0.5f)), int (round(m_center.Y() - h * 0.5f)), int(round(w)), int(round(h)));
}


void Viewport::SetResized(int deltaLeft, int deltaTop, int deltaWidth, int deltaHeight) const {
    baseRenderer.SetViewport (Resized (deltaLeft, deltaTop, deltaWidth, deltaHeight));
}


void Viewport::BuildTransformation(int windowWidth, int windowHeight, bool flipVertically) noexcept {
    float sx = Widthf() / float(windowWidth);
    float sy = Heightf() / float(windowHeight);
    
    if (flipVertically) 
        sy = -sy; // Inhalte im Rect vertikal spiegeln

    float tx = 2.0f * (Leftf() + 0.5f * Widthf()) / float(windowWidth) - 1.0f;
    float ty = 1.0f - 2.0f * (Topf() + 0.5f * Heightf()) / float(windowHeight);
#if 1
    glm::mat4 M(1.0f);
#   if 1
    M[0][0] = sx;           // scale x
    M[1][1] = sy;           // scale y (negativ => vertikal gespiegelt)
    M[3][0] = tx;           // Offsets in W-Spalte (Translation)
    M[3][1] = ty;
#   endif
    m_transformation = Matrix4f(M);
#else
    m_transformation = Matrix4f({
          sx, 0.0f, 0.0f, 0.0f, // col 0
        0.0f,   sy, 0.0f, 0.0f, // col 1
        0.0f, 0.0f, 1.0f, 0.0f, // col 2
          tx,   ty, 0.0f, 1.0f  // col 3 (Translation in W-Spalte)
        });
#endif
}

// =================================================================================================
