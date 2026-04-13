#pragma once

#include <cstdint>

// =================================================================================================
// GfxDriverTypes — DirectX 12-specific type aliases.
// Shared code uses GfxDriverTypes::Int, GfxDriverTypes::Uint etc. The correct definition is
// resolved via the project's include path (directx/include/ takes precedence over shared include/).
//
// All types map to standard C++ fixed-width types; no dependency on GL or DX headers here.
// DX12 API calls use these directly (LONG = int32_t, UINT = uint32_t, FLOAT = float, etc.)

namespace GfxDriverTypes {
    using Int    = int32_t;   // signed integer parameter (viewport coords, etc.)
    using Uint   = uint32_t;  // unsigned integer (descriptor indices, counts, …)
    using Float  = float;     // floating-point parameter (matrices, colors)
    using Enum   = uint32_t;  // enumeration token (mapped from GL compat values or DX12 enums)
    using Handle = uint32_t;  // GPU resource handle (SRV/RTV/DSV descriptor-heap index)
}

// =================================================================================================
