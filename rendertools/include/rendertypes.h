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
    ClampToEdge = 1,
    // Samples outside [0,1] return TextureSampling::borderColor. Needed by the shadow sampler, where the
    // area outside the shadow map has to read as "no shadow" (white border) rather than smearing the edge.
    ClampToBorder = 2
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
    // Packed HDR, 4 bytes per texel and no alpha: 11+11 bits mantissa/exponent for red and green,
    // 10 for blue. Colour-renderable everywhere, which the similarly sized RGB9_E5 is not - that one
    // is texture only, so nothing can be rendered INTO it. Half the memory of RGBA16_SFloat at the
    // precision a light map needs.
    RG11B10_SFloat,
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
        case GfxPixelFormat::RG11B10_SFloat:
            return 4;
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

    // The named COMBINATIONS a renderer draws with, one step up from the factors above. A draw asks for
    // what it wants to look like - "additive", "alpha" - and not for a pair of factors, so the factors
    // stay in one place and a backend that cannot set them per draw (Vulkan bakes blending into the
    // pipeline) has one value to key its pipeline on instead of two.
    //
    // Deliberately NOT a list of every possible pair: these are the ones a renderer actually uses, and
    // anything else belongs in this list before it belongs at a call site.
    enum class BlendMode : uint8_t {
        Replace,            // One, Zero - overwrite, blending effectively off
        Alpha,              // SrcAlpha, InvSrcAlpha - the ordinary transparency blend
        Additive,           // One, One - light adds to light; the tone mapper caps the range, not the blend
        Multiply,           // DstColor, Zero - darkening overlays
        AlphaControlled     // One, SrcAlpha - additive whose strength the source's alpha decides
    };

    // The factor pair one mode stands for. Shared by every backend - what differs between them is only
    // what they do with the pair, not what the pair is.
    inline constexpr void BlendFactors(BlendMode mode, BlendFactor& src, BlendFactor& dst) noexcept {
        switch (mode) {
            case BlendMode::Replace:         src = BlendFactor::One;      dst = BlendFactor::Zero;        break;
            case BlendMode::Additive:        src = BlendFactor::One;      dst = BlendFactor::One;         break;
            case BlendMode::Multiply:        src = BlendFactor::DstColor; dst = BlendFactor::Zero;        break;
            case BlendMode::AlphaControlled: src = BlendFactor::One;      dst = BlendFactor::SrcAlpha;    break;
            case BlendMode::Alpha:
            default:                         src = BlendFactor::SrcAlpha; dst = BlendFactor::InvSrcAlpha; break;
        }
    }

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
