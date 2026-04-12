#pragma once

// =================================================================================================
// GfxTypes — OpenGL-specific definitions.
// Shared code uses GfxTypes::Int, GfxTypes::Uint etc. The correct definition is resolved via
// the project's include path (opengl/include/ takes precedence over the shared include/).
//
// GLint, GLuint, GLfloat, GLenum are the authoritative types for OpenGL API calls.
// If the GL spec changes the underlying widths, only this file needs updating.

#include "glew.h"

namespace GfxTypes {
    using Int    = GLint;    // signed integer parameter (e.g. viewport coords, query results)
    using Uint   = GLuint;   // unsigned integer / object name (textures, buffers, VAOs, …)
    using Float  = GLfloat;  // floating-point parameter (matrices, colors)
    using Enum   = GLenum;   // enumeration token passed to GL API
    using Handle = GLuint;   // GPU resource name / handle
}

// =================================================================================================
