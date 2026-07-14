#pragma once

#include <cstdint>
#include "gfxtypes.h"   // GfxTypes::Int/Uint/Float/Enum/Handle — resolved per API via include path

// =================================================================================================
// API-neutral enumerations for mesh topology, component data types, and texture types.
// OGL implementations convert these to GLenum internally; DX12 implementations use DXGI_FORMAT etc.

enum class MeshTopology : uint8_t {
    Quads = 0,
    Triangles = 1,
    Lines = 2,
    Points = 3
};

enum class ComponentType : uint8_t {
    Float = 0,
    UInt32 = 1,
    UInt16 = 2
};

enum class GfxBufferTarget : uint8_t {
    Vertex = 0,
    Index  = 1
};

enum class GfxWrapMode : uint8_t {
    Repeat = 0,
    ClampToEdge = 1
};

enum class TextureType : uint8_t {
    Texture2D = 0,
    Texture3D = 1,
    CubeMap = 2
};

// Platform-neutral pixel formats used for textures uploaded from CPU-side data buffers.
// Each backend maps these to its native format type (GLenum / VkFormat / DXGI_FORMAT) via a
// per-backend helper (To<Api>Format / GfxPixelStride). Names follow Vulkan conventions for
// channel layout (e.g. RGBA32_SFloat = 4 channels x float32) so they remain unambiguous
// regardless of which backend reads them.
enum class GfxPixelFormat : uint8_t {
    R8_UNorm = 0,
    RG8_UNorm,
    RGBA8_UNorm,
    R16_SFloat,
    R32_SFloat,
    RGBA16_SFloat,
    RGBA32_SFloat,
    // Block-compressed formats (DDS-backed, GPU-native). Data is organized in 4x4 texel blocks,
    // so GfxPixelStride does not apply — use GfxBlockBytes / GfxIsBlockCompressed instead.
    BC1_UNorm,      // RGB (1-bit punch-through alpha), 8 bytes / 4x4 block  (DXT1)
    BC7_UNorm,      // RGBA, 16 bytes / 4x4 block
    BC4_UNorm,      // 1 channel (grayscale: AO / spec / roughness), 8 bytes / 4x4 block  (RGTC1)
    BC5_UNorm       // 2 channels (tangent normal XY, Z reconstructed), 16 bytes / 4x4 block  (RGTC2)
};

// Bytes per pixel (sum across channels). Used by upload paths to compute row strides without
// touching API-native format descriptors.
inline constexpr uint32_t GfxPixelStride(GfxPixelFormat f) noexcept {
    switch (f) {
        case GfxPixelFormat::R8_UNorm:       
            return 1;
        case GfxPixelFormat::RG8_UNorm:      
            return 2;
        case GfxPixelFormat::RGBA8_UNorm:    
            return 4;
        case GfxPixelFormat::R16_SFloat:     
            return 2;
        case GfxPixelFormat::R32_SFloat:     
            return 4;
        case GfxPixelFormat::RGBA16_SFloat:  
            return 8;
        case GfxPixelFormat::RGBA32_SFloat:  
            return 16;
        case GfxPixelFormat::BC1_UNorm:
        case GfxPixelFormat::BC7_UNorm:
        case GfxPixelFormat::BC4_UNorm:
        case GfxPixelFormat::BC5_UNorm:
            return 0;   // block-compressed: stride is per 4x4 block, see GfxBlockBytes
    }
    return 0;
}


// True for block-compressed (BCn) formats. Their data is stored in 4x4 texel blocks, so the
// per-pixel GfxPixelStride is meaningless — upload paths must use block math (GfxBlockBytes).
inline constexpr bool GfxIsBlockCompressed(GfxPixelFormat f) noexcept {
    return (f == GfxPixelFormat::BC1_UNorm) or (f == GfxPixelFormat::BC7_UNorm)
        or (f == GfxPixelFormat::BC4_UNorm) or (f == GfxPixelFormat::BC5_UNorm);
}

// Bytes per 4x4 texel block for block-compressed formats; 0 for uncompressed formats. A full mip
// level occupies ceil(w/4) * ceil(h/4) * GfxBlockBytes bytes.
inline constexpr uint32_t GfxBlockBytes(GfxPixelFormat f) noexcept {
    switch (f) {
        case GfxPixelFormat::BC1_UNorm:  return 8;
        case GfxPixelFormat::BC4_UNorm:  return 8;
        case GfxPixelFormat::BC7_UNorm:  return 16;
        case GfxPixelFormat::BC5_UNorm:  return 16;
        default:                         return 0;
    }
}


// Game-facing texture-compression descriptor (mirrors the block-compressed GfxPixelFormat values;
// tcNone = uncompressed). Stored per Texture so game code / shaders can react to it — e.g. tell the
// wall shader that a normal map is BC5 (2-channel, reconstruct Z) rather than an uncompressed PNG.
enum eTextureCompression {
    tcNone = 0,
    tcBC1,
    tcBC4,
    tcBC5,
    tcBC7
};

// Map a loaded GfxPixelFormat to the game-facing compression descriptor.
inline eTextureCompression GfxFormatToCompression(GfxPixelFormat f) noexcept {
    switch (f) {
        case GfxPixelFormat::BC1_UNorm: return tcBC1;
        case GfxPixelFormat::BC4_UNorm: return tcBC4;
        case GfxPixelFormat::BC5_UNorm: return tcBC5;
        case GfxPixelFormat::BC7_UNorm: return tcBC7;
        default:                        return tcNone;
    }
}

// =================================================================================================
// API-neutral render state constants.

namespace GfxOperations {

    enum class CompareFunc : uint8_t {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    enum class BlendFactor : uint8_t {
        Zero,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DstAlpha,
        InvDstAlpha,
        DstColor,
        InvDstColor
    };

    enum class BlendOp : uint8_t {
        Add,
        Subtract,
        RevSubtract,
        Min,
        Max
    };

    enum class CullFace : uint8_t {
        Front,
        Back,
        None
    };

    enum class Winding : uint8_t {
        Regular,
        Reverse
    };

    enum class StencilOp : uint8_t {
        Keep,
        Zero,
        Replace,
        IncrSat,
        DecrSat,
        Incr,
        Decr
    };

    enum class BufferFlag : GfxTypes::Bitfield {
        Color = 1,
        Depth = 2,
		Stencil = 4
    };

}

// =================================================================================================
