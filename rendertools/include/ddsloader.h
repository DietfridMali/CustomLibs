#pragma once

#include "string.hpp"

#include <cstring>

class TextureBuffer;

// True if the path ends in the ".dds" extension (case-insensitive). The per-backend Texture::Load
// loop uses this to route a file to LoadDDS instead of the SDL image loader.
inline bool IsDDSFile(const String& path) noexcept {
    const char* s = (const char*) path;
    if (s == nullptr)
        return false;
    const size_t n = strlen(s);
    if (n < 4)
        return false;
    const char* t = s + (n - 4);
    auto lower = [](char c) noexcept { return ((c >= 'A') and (c <= 'Z')) ? char(c - 'A' + 'a') : c; };
    return (t[0] == '.') and (lower(t[1]) == 'd') and (lower(t[2]) == 'd') and (lower(t[3]) == 's');
}

// =================================================================================================
// Minimal DDS reader for the block-compressed skybox / icon assets produced by the offline
// pipeline (Compressonator / texconv). Recognizes only the two formats we ship:
//   • BC1 — classic "DXT1" FourCC, or DX10-extended DXGI_FORMAT_BC1_UNORM(_SRGB)
//   • BC7 — DX10-extended DXGI_FORMAT_BC7_UNORM(_SRGB)
// Optionally mip-mapped, 2D only. On success buf.m_info carries the GfxPixelFormat, width, height,
// mip count and total payload size, and buf.m_data holds every mip level tightly packed (level 0
// first). Cubemap faces stay one file per face — the existing Texture::Load loop assembles them —
// so array / cubemap DDS containers are intentionally not handled here.
//
// Returns false (and logs to stderr) on I/O error, a malformed header, or any unsupported format.

bool LoadDDS(const String& path, TextureBuffer& buf) noexcept;

// =================================================================================================
// Unified texture-file loader shared by every backend's Texture::Load, so the DDS-vs-PNG decision
// (and the path joining) lives in exactly one place. Returns a newly-allocated TextureBuffer (the
// caller owns it) or nullptr on failure (logged to stderr when isRequired).
//   • allowDDS && a sibling "<base>.dds" exists next to fileName  → load that DDS
//   • fileName already ends in ".dds"                             → load it as DDS
//   • otherwise                                                   → SDL image loader (PNG etc.)
// allowDDS is passed false by backends whose Deploy can't upload block-compressed data yet.

TextureBuffer* LoadTextureFile(const String& folder, const String& fileName,
                               bool premultiply, bool flipVertically, bool isRequired,
                               bool allowDDS) noexcept;

// =================================================================================================
