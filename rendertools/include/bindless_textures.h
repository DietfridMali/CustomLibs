#pragma once

#include <cstdint>

// =================================================================================================

namespace BindlessTextures {

inline constexpr uint32_t kInvalidIndex = UINT32_MAX;
inline constexpr uint32_t kSpace2D = 3;
inline constexpr uint32_t kSpace2DArray = 4;
inline constexpr uint32_t kSpaceCube = 5;
inline constexpr uint32_t kSpaceSamplers = 3;

inline constexpr const char* kHlslDeclarations = R"(
Texture2D		bindless2D [] : register(t0, space3);
Texture2DArray	bindless2DArray [] : register(t0, space4);
TextureCube		bindlessCube [] : register(t0, space5);
SamplerState	bindlessSamplers [] : register(s0, space3);
)";

}

// =================================================================================================
