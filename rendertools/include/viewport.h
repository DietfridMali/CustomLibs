#pragma once

#include "rectangle.h"
#include "vector.hpp"
#include "base_quad.h"
#include "colordata.h"

// =================================================================================================

class Viewport : public Rectangle 
{
public:
    Viewport(int left = 0, int top = 0, int width = 0, int height = 0)
        : Rectangle(left, top, width, height) {
    }

    Viewport(Rectangle& r) 
        : Rectangle (r.m_left, r.m_top, r.m_width, r.m_height) 
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

    void SetViewport(void);

    Viewport Resize(int deltaLeft, int deltaTop, int deltaWidth, int deltaHeight);

    void SetResized(int deltaLeft, int deltaTop, int deltaWidth, int deltaHeight);

};

// =================================================================================================
