
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

const String& Standard2DVS() {
    static const String shader(
        R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 texCoord;
            uniform mat4 mModelView;
            uniform mat4 mProjection;
            uniform mat4 mViewport;
            out vec3 fragPos;
            out vec2 fragTexCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mViewport * mProjection * viewPos;
                fragTexCoord = texCoord;
                fragPos = viewPos.xyz;
                }
        )"
    );
    return shader;
}

const String& Standard3DVS() {
    static const String shader(
        R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 texCoord;
            uniform mat4 mModelView;
            uniform mat4 mProjection;
            out vec3 fragPos;
            out vec2 fragTexCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mProjection * viewPos;
                fragTexCoord = texCoord;
                fragPos = viewPos.xyz;
                }
        )"
    );
    return shader;
}

const String& Offset2DVS() {
    static const String shader(
        R"(
            //#version 140
            //#extension GL_ARB_explicit_attrib_location : enable
            #version 330
            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 texCoord;
            uniform mat4 mModelView;
            uniform mat4 mProjection;
            uniform mat4 mViewport;
            uniform float offset;
            out vec3 fragPos;
            out vec2 fragTexCoord;
            void main() {
                vec4 viewPos = mModelView * vec4 (position, 1.0);
                gl_Position = mViewport * mProjection * vec4(viewPos.x + offset, viewPos.y + offset, viewPos.z, 1.0);
                fragTexCoord = texCoord;
                fragPos = viewPos.xyz;
                }
        )"
    );
    return shader;
}


// =================================================================================================
