#include "ddsloader.h"
#include "rendertypes.h"
#include "texturebuffer.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>

// =================================================================================================
// DDS parsing. The container is little-endian; every target platform (x64 / ARM64 desktop + Xbox)
// is little-endian too, so the 32-bit fields are read straight out of the byte buffer.
//
// Layout:  "DDS " magic (4)  +  DDS_HEADER (124)  [ + DDS_HEADER_DXT10 (20) if FourCC == "DX10" ]
//          followed by the tightly packed mip chain (level 0 first).

namespace {

constexpr uint32_t kDDSMagic    = 0x20534444; // 'D','D','S',' '
constexpr uint32_t kFourCC_DXT1 = 0x31545844; // 'D','X','T','1'
constexpr uint32_t kFourCC_DX10 = 0x30315844; // 'D','X','1','0'
constexpr uint32_t kDDPF_FOURCC = 0x4;        // DDS_PIXELFORMAT.dwFlags bit: FourCC field is valid

// DXGI_FORMAT values carried by the DX10-extended header. Kept local so this stays platform-neutral
// (no dxgiformat.h, which is DirectX-only).
constexpr uint32_t kDXGI_BC1_UNORM      = 71;
constexpr uint32_t kDXGI_BC1_UNORM_SRGB = 72;
constexpr uint32_t kDXGI_BC7_UNORM      = 98;
constexpr uint32_t kDXGI_BC7_UNORM_SRGB = 99;

// Field offsets inside DDS_HEADER (relative to the byte right after the 4-byte magic).
constexpr size_t kOffHeight     = 8;
constexpr size_t kOffWidth      = 12;
constexpr size_t kOffMipCount   = 24;
constexpr size_t kOffPixelFmt   = 72;  // DDS_PIXELFORMAT starts here (32 bytes)
constexpr size_t kOffPfFlags    = kOffPixelFmt + 4;
constexpr size_t kOffFourCC     = kOffPixelFmt + 8;

constexpr size_t kMaxMipLevels  = 16;  // sanity cap (covers up to 65536 px) against garbage counts

inline uint32_t ReadU32(const uint8_t* p) noexcept {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// Number of 4x4 blocks spanning a dimension of x texels (rounded up).
inline uint32_t BlocksAcross(uint32_t x) noexcept {
    return (x + 3u) / 4u;
}

} // namespace


bool LoadDDS(const String& path, TextureBuffer& buf) noexcept {
    const char* name = (const char*) path;

    std::ifstream f(name, std::ios::binary | std::ios::ate);
    if (not f.is_open()) {
        fprintf(stderr, "LoadDDS: cannot open '%s'\n", name);
        return false;
    }
    const std::streamoff fileSize = f.tellg();
    if (fileSize < std::streamoff(4 + 124)) {
        fprintf(stderr, "LoadDDS: '%s' is too small to be a DDS file\n", name);
        return false;
    }
    f.seekg(0, std::ios::beg);

    // Magic + fixed 124-byte header. A DX10 header (if present) and the payload follow.
    uint8_t header[4 + 124];
    f.read(reinterpret_cast<char*>(header), std::streamsize(sizeof(header)));
    if (not f) {
        fprintf(stderr, "LoadDDS: '%s' header read failed\n", name);
        return false;
    }
    if (ReadU32(header) != kDDSMagic) {
        fprintf(stderr, "LoadDDS: '%s' is not a DDS file (bad magic)\n", name);
        return false;
    }

    const uint8_t* hdr        = header + 4;                 // start of DDS_HEADER
    const uint32_t height     = ReadU32(hdr + kOffHeight);
    const uint32_t width      = ReadU32(hdr + kOffWidth);
    const uint32_t mipMapCnt  = ReadU32(hdr + kOffMipCount);
    const uint32_t pfFlags    = ReadU32(hdr + kOffPfFlags);
    const uint32_t fourCC     = ReadU32(hdr + kOffFourCC);

    if ((pfFlags & kDDPF_FOURCC) == 0) {
        fprintf(stderr, "LoadDDS: '%s' is uncompressed; only BC1/BC7 DDS are supported\n", name);
        return false;
    }
    if ((width == 0) or (height == 0)) {
        fprintf(stderr, "LoadDDS: '%s' has zero dimensions\n", name);
        return false;
    }

    GfxPixelFormat format = GfxPixelFormat::RGBA8_UNorm;
    size_t         extraHeader = 0;   // DX10 header size, when present

    if (fourCC == kFourCC_DXT1) {
        format = GfxPixelFormat::BC1_UNorm;
    }
    else if (fourCC == kFourCC_DX10) {
        extraHeader = 20;
        uint8_t dx10[20];
        f.read(reinterpret_cast<char*>(dx10), std::streamsize(sizeof(dx10)));
        if (not f) {
            fprintf(stderr, "LoadDDS: '%s' DX10 header read failed\n", name);
            return false;
        }
        const uint32_t dxgiFormat = ReadU32(dx10);
        if ((dxgiFormat == kDXGI_BC1_UNORM) or (dxgiFormat == kDXGI_BC1_UNORM_SRGB))
            format = GfxPixelFormat::BC1_UNorm;
        else if ((dxgiFormat == kDXGI_BC7_UNORM) or (dxgiFormat == kDXGI_BC7_UNORM_SRGB))
            format = GfxPixelFormat::BC7_UNorm;
        else {
            fprintf(stderr, "LoadDDS: '%s' has unsupported DXGI format %u (need BC1/BC7)\n", name, dxgiFormat);
            return false;
        }
    }
    else {
        fprintf(stderr, "LoadDDS: '%s' has unsupported FourCC 0x%08X (need DXT1 or DX10 BC7)\n", name, fourCC);
        return false;
    }

    uint32_t mipCount = (mipMapCnt > 0) ? mipMapCnt : 1u;
    if (mipCount > kMaxMipLevels)
        mipCount = kMaxMipLevels;

    const uint32_t blockBytes = GfxBlockBytes(format);

    // Total payload = sum over mip levels of ceil(w/4) * ceil(h/4) * blockBytes.
    size_t   expected = 0;
    uint32_t w = width, h = height;
    for (uint32_t i = 0; i < mipCount; ++i) {
        expected += size_t(BlocksAcross(w)) * size_t(BlocksAcross(h)) * blockBytes;
        w = (w > 1u) ? (w >> 1) : 1u;
        h = (h > 1u) ? (h >> 1) : 1u;
    }

    const std::streamoff payloadOffset = std::streamoff(4 + 124) + std::streamoff(extraHeader);
    const size_t         available     = size_t(fileSize - payloadOffset);
    if (available < expected) {
        fprintf(stderr, "LoadDDS: '%s' payload too small (%llu < %llu bytes)\n",
                name, (unsigned long long) available, (unsigned long long) expected);
        return false;
    }

    // Keep exactly the expected mip-chain bytes; ignore any trailing padding the encoder may add.
    buf.m_info.m_width          = int32_t(width);
    buf.m_info.m_height         = int32_t(height);
    buf.m_info.m_componentCount = 0;                 // not applicable to block-compressed data
    buf.m_info.m_internalFormat = 0;
    buf.m_info.m_format         = 0;
    buf.m_info.m_gfxFormat      = format;
    buf.m_info.m_mipCount       = int32_t(mipCount);
    buf.m_info.m_dataSize       = int32_t(expected);

    buf.m_data.Resize(uint32_t(expected));
    if (uint32_t(buf.m_data.Length()) < uint32_t(expected)) {
        fprintf(stderr, "LoadDDS: '%s' out of memory for %llu bytes\n", name, (unsigned long long) expected);
        return false;
    }

    f.seekg(payloadOffset, std::ios::beg);
    f.read(reinterpret_cast<char*>(buf.m_data.Data()), std::streamsize(expected));
    if (not f) {
        fprintf(stderr, "LoadDDS: '%s' payload read failed\n", name);
        return false;
    }

    return true;
}

// =================================================================================================
