#pragma once

// =================================================================================================
// GfxDriverTypes — OpenGL-specific type aliases.
// Shared code uses GfxDriverTypes::Int, GfxDriverTypes::Uint etc. The correct definition is
// resolved via the project's include path (opengl/include/ takes precedence over shared include/).
//
// GLint, GLuint, GLfloat, GLenum are the authoritative types for OpenGL driver calls.
// If the GL spec changes the underlying widths, only this file needs updating.

#include "glew.h"

// glew.h pulls in windows.h/wingdi.h which defines Rectangle as a GDI macro.
// Kill it here so any file that includes this header keeps our Rectangle class visible.
#ifdef Rectangle
#undef Rectangle
#endif

namespace GfxDriverTypes {
    using Int    = GLint;    // signed integer parameter (e.g. viewport coords, query results)
    using Uint   = GLuint;   // unsigned integer / object name (textures, buffers, VAOs, …)
    using Float  = GLfloat;  // floating-point parameter (matrices, colors)
    using Enum   = GLenum;   // enumeration token passed to GL driver
    using Handle = GLuint;   // GPU resource name / handle
}

// =================================================================================================
