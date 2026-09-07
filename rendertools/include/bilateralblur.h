#pragma once

#include "shader.h"
#include "vector.hpp"

// =================================================================================================
// The parameters of the shared bilateral blur (the "bilateralBlur" shader, blur_shader.cpp).
//
// The shader is one for both users; what differs is where the SURFACE DISTANCE comes from, and that is
// what distanceSource selects:
//
//   dsWorldPosition  a world position G-buffer is bound on t2, and the distance is the neighbour's
//                    offset from the centre pixel's tangent plane
//   dsSceneDepth     the scene's depth buffer is bound on t3, and the distance is the difference of
//                    the two linearized eye space depths. projDepth carries (A, B) of the projection
//                    matrix for that, so neither the near nor the far plane has to be passed.
//
// The normals (t1) and the image being filtered (t0) are the same in both cases. Binding the textures
// is the caller's business - only it knows which target they live on.

struct BilateralBlurParams {
    enum eDistanceSource {
        dsWorldPosition = 0,
        dsSceneDepth = 1
    };

    Vector2f    texelSize{ Vector2f(0.0f, 0.0f) };  // of the buffer being FILTERED, not of the frame
    float       direction{ 0.0f };                  // 0 = along x, 1 = along y
    int         radius{ 9 };                        // taps per side
    float       normalPower{ 8.0f };                // orientation edge stop
    float       posSigma{ 0.4f };                   // surface distance tolerance, world units
    int         distanceSource{ dsWorldPosition };
    Vector2f    projDepth{ Vector2f(0.0f, 0.0f) };  // (A, B), dsSceneDepth only
    bool        flipVertically{ false };            // OpenGL only, see the note in the shader
};

// Sets everything the shader reads. The caller deploys it and binds the textures itself - the two
// projects reach their shader handler through different types, and which target a texture sits on is
// nothing this could know.
//
// The sampler units matter to OpenGL only - the HLSL sources bind their textures by register, so there
// is no constant of that name there. Setting one all the same is what the callers have always done
// with flipVertically, which the HLSL sources do not have either: a name the shader does not know is
// dropped. Asking the API type instead would mean UsesOpenGL () from gfxapitype.h, and that one cannot
// answer here - gfxApiType is declared "inline static", so every translation unit carries its OWN copy
// and the library's is not the application's. That is why the app asks its renderer, not a free
// function.

inline void SetupBilateralBlur(Shader* pShader, const BilateralBlurParams& params) {
    if (pShader == nullptr)
        return;
    pShader->SetInt("surface", 0);
    pShader->SetInt("uWorldNormals", 1);
    pShader->SetInt("uWorldPositions", 2);
    pShader->SetInt("uSceneDepth", 3);
    pShader->SetVector2f("texelSize", params.texelSize);
    pShader->SetFloat("direction", params.direction);
    pShader->SetInt("radius", params.radius);
    pShader->SetFloat("normalPower", params.normalPower);
    pShader->SetFloat("posSigma", params.posSigma);
    pShader->SetInt("distanceSource", params.distanceSource);
    pShader->SetVector2f("projDepth", params.projDepth);
    pShader->SetInt("flipVertically", params.flipVertically ? 1 : 0);
}

// =================================================================================================
