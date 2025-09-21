#include <utility>
#include "glew.h"
#include "viewport.h"
#include "base_renderer.h"

// =================================================================================================

void Viewport::Fill(const RGBColor& color, float alpha, float scale) {
    SetViewport();
    baseRenderer.Fill(color, alpha, scale);
}


void Viewport::SetViewport(void) {
    baseRenderer.SetViewport(*this);
}


Viewport Viewport::Resize(int deltaLeft, int deltaTop, int deltaWidth, int deltaHeight) const {
    return Viewport (m_left + deltaLeft, m_top + deltaTop, m_width + deltaWidth, m_height + deltaHeight);
}


Viewport Viewport::Resize(float scale) const {
    if (scale == 1.0f)
        return *this;
    int w = int(roundf((float(m_width) * scale)));
    int h = int(roundf((float(m_height) * scale)));
    if (w * h == 0)
        return *this;
    int dw = (m_width - w) / 2;
    int dh = (m_height - h) / 2;
    return Viewport(m_left + dw, m_top + dh, w, h);
}


void Viewport::SetResized(int deltaLeft, int deltaTop, int deltaWidth, int deltaHeight) const {
    baseRenderer.SetViewport (Resize (deltaLeft, deltaTop, deltaWidth, deltaHeight));
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
