#pragma once

#include <cstdint>
#include "vector.hpp"
#include "colordata.h"
#include "gfxtypes.h"
#include "gfxarray.hpp"
#include "base_quadmesh.h"

// =================================================================================================
// LineRenderer - the library's line drawer, one shader for all three APIs ("lineDraw", see
// <backend>/src/line_shader.cpp).
//
// Every line is one instance of a unit quad that the vertex shader expands into a ribbon in view
// space; the fragment shader draws a capsule distance field into it, so the ends are round, joints
// of a strip close by themselves, and the edge is antialiased analytically. The width is in PIXELS
// of the target and stays that on screen whatever the distance - what glLineWidth did in OpenGL, and
// what DX12 has no counterpart for. Works under a perspective projection (3D lines) and under an
// orthographic one (HUD / 2D lines) alike.
//
// Use: Clear (), Add () / AddStrip () as often as wanted, Render () once. The records are uploaded
// on Render (), the buffer grows on demand. The renderer sets alpha blending and no face culling for
// its draw and puts both back; depth test / write are the caller's - a HUD line wants them off, a
// line in the scene wants them on.
//
// Patterns: Solid, Dashed, Dotted, DashDot. The lengths are multiples of the line width (a dot is
// the round cap alone), m_dashScale stretches them. AddStrip () keeps the pattern running across the
// joints; a single Add () can do the same with the phase argument (the pattern length already used up
// before p0, in view units).

class LineRenderer {
public:
    enum class Style : uint8_t {
        Solid = 0,
        Dashed = 1,
        Dotted = 2,
        DashDot = 3
    };

    // One line as the shader reads it (GPU structured buffer, 64 bytes). Position vectors are in
    // whatever space the current model view maps to view space.
    struct Line {
        Vector3f    p0;
        float       width;      // target pixels
        Vector3f    p1;
        float       style;      // Style as float - the shader reads it as such
        RGBAColor   color;
        float       phase;      // pattern offset at p0, view units
        float       pad[3];
    };

    float           m_dashScale{ 1.0f };
    bool            m_antialias{ true };

    LineRenderer() = default;
    ~LineRenderer() { Destroy(); }

    bool Create(int capacity = 256);
    void Destroy(void);

    inline bool IsAvailable(void) const noexcept { return m_isAvailable; }
    inline int Count(void) const noexcept { return m_count; }

    // start a new batch
    inline void Clear(void) noexcept { m_count = 0; }

    bool Add(const Vector3f& p0, const Vector3f& p1, float width, const RGBAColor& color, Style style = Style::Solid, float phase = 0.0f);

    // A polyline through count points (closed: last back to first), the pattern continuous over the
    // joints. The phase is accumulated from the point distances, i.e. in the caller's units - equal to
    // view units as long as the model view carries no scale.
    bool AddStrip(const Vector3f* points, int count, float width, const RGBAColor& color, Style style = Style::Solid, bool closed = false);

    // Draws what was added since Clear () with the current matrices and viewport.
    bool Render(void);

private:
    GfxArray<Line, GfxTypes::StructuredBuffer>  m_buffer;
    int                                         m_capacity{ 0 };
    int                                         m_count{ 0 };
    BaseQuadMesh                                m_quad;
    bool                                        m_quadReady{ false };
    bool                                        m_isAvailable{ false };

    bool Reserve(int count);
    void SetupQuad(void);
};

static_assert(sizeof(LineRenderer::Line) == 64, "LineRenderer::Line must stay 64 bytes (GPU StructuredBuffer layout)");

// =================================================================================================
