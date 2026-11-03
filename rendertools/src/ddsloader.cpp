#include "ddsloader.h"
#include "rendertypes.h"
#include "texturebuffer.h"
#include "gfxrenderer.h"
#include "loghandler.h"
#include "missingfiles.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <filesystem>

// =================================================================================================
// DDS parsing. The container is little-endian; every target platform (x64 / ARM64 desktop + Xbox)
// is little-endian too, so the 32-bit fields are read straight out of the byte buffer.
//
// Layout:  "DDS " magic (4)  +  DDS_HEADER (124)  [ + DDS_HEADER_DXT10 (20) if FourCC == "DX10" ]
//          followed by the tightly packed mip chain (level 0 first).

namespace {

constexpr uint32_t kDDSMagic = 0x20534444; // 'D','D','S',' '
constexpr uint32_t kFourCC_DXT1 = 0x31545844; // 'D','X','T','1'
constexpr uint32_t kFourCC_DX10 = 0x30315844; // 'D','X','1','0'
constexpr uint32_t kFourCC_ATI1 = 0x31495441; // 'A','T','I','1'  (BC4, classic FourCC)
constexpr uint32_t kFourCC_BC4U = 0x55344342; // 'B','C','4','U'
constexpr uint32_t kFourCC_ATI2 = 0x32495441; // 'A','T','I','2'  (BC5, classic FourCC)
constexpr uint32_t kFourCC_BC5U = 0x55354342; // 'B','C','5','U'
constexpr uint32_t kDDPF_FOURCC = 0x4; // DDS_PIXELFORMAT.dwFlags bit: FourCC field is valid

// DXGI_FORMAT values carried by the DX10-extended header. Kept local so this stays platform-neutral
// (no dxgiformat.h, which is DirectX-only).
constexpr uint32_t kDXGI_BC1_UNORM = 71;
constexpr uint32_t kDXGI_BC1_UNORM_SRGB = 72;
constexpr uint32_t kDXGI_BC7_UNORM = 98;
constexpr uint32_t kDXGI_BC7_UNORM_SRGB = 99;
constexpr uint32_t kDXGI_BC4_UNORM = 80;
constexpr uint32_t kDXGI_BC5_UNORM = 83;

// Field offsets inside DDS_HEADER (relative to the byte right after the 4-byte magic).
constexpr size_t kOffHeight = 8;
constexpr size_t kOffWidth = 12;
constexpr size_t kOffMipCount = 24;
constexpr size_t kOffPixelFmt = 72; // DDS_PIXELFORMAT starts here (32 bytes)
constexpr size_t kOffPfFlags = kOffPixelFmt + 4;
constexpr size_t kOffFourCC = kOffPixelFmt + 8;

constexpr size_t kMaxMipLevels = 16; // sanity cap (covers up to 65536 px) against garbage counts

inline uint32_t ReadU32(const uint8_t* p)
noexcept
{
	uint32_t v;
	std::memcpy(&v, p, sizeof(v));
	return v;
}

// Number of 4x4 blocks spanning a dimension of x texels (rounded up).
inline uint32_t BlocksAcross(uint32_t x)
noexcept
{
	return (x + 3u) / 4u;
}

bool useFileColorEncoding = false;

} // namespace


void SetUseFileColorEncoding(bool use)
noexcept
{
	useFileColorEncoding = use;
}


namespace {

constexpr size_t kDDSHeaderSize = 4 + 124;
constexpr size_t kDDSHeaderDX10Size = 20;

struct DDSLayout {
	GfxPixelFormat	format{ GfxPixelFormat::RGBA8_UNorm };
	uint32_t		width{ 0 };
	uint32_t		height{ 0 };
	uint32_t		mipCount{ 1 };
	size_t			payloadOffset{ 0 };
	size_t			payloadSize{ 0 };
	eColorEncoding	colorEncoding{ ecLinear };
	bool			hasColorEncoding{ false };
};

bool ParseDDSHeader(const char* name, const uint8_t* header, size_t headerSize, size_t fileSize, DDSLayout& layout)
noexcept
{
	if (ReadU32(header) != kDDSMagic) {
		logHandler.Print("LoadDDS: '%s' is not a DDS file (bad magic)\n", name);
		return false;
	}

	const uint8_t* hdr = header + 4; // start of DDS_HEADER
	const uint32_t height = ReadU32(hdr + kOffHeight);
	const uint32_t width = ReadU32(hdr + kOffWidth);
	const uint32_t mipMapCnt = ReadU32(hdr + kOffMipCount);
	const uint32_t pfFlags = ReadU32(hdr + kOffPfFlags);
	const uint32_t fourCC = ReadU32(hdr + kOffFourCC);

	if ((pfFlags & kDDPF_FOURCC) == 0) {
		logHandler.Print("LoadDDS: '%s' is uncompressed; only BC1/BC7 DDS are supported\n", name);
		return false;
	}
	if ((width == 0) or (height == 0)) {
		logHandler.Print("LoadDDS: '%s' has zero dimensions\n", name);
		return false;
	}

	GfxPixelFormat	format = GfxPixelFormat::RGBA8_UNorm;
	size_t			extraHeader = 0; // DX10 header size, when present
	eColorEncoding	colorEncoding = ecLinear;
	bool			hasColorEncoding = false;

	if (fourCC == kFourCC_DXT1) {
		format = GfxPixelFormat::BC1_UNorm;
	}
	else if ((fourCC == kFourCC_ATI1) or (fourCC == kFourCC_BC4U)) {
		format = GfxPixelFormat::BC4_UNorm;
	}
	else if ((fourCC == kFourCC_ATI2) or (fourCC == kFourCC_BC5U)) {
		format = GfxPixelFormat::BC5_UNorm;
	}
	else if (fourCC == kFourCC_DX10) {
		extraHeader = kDDSHeaderDX10Size;
		if (headerSize < kDDSHeaderSize + kDDSHeaderDX10Size) {
			logHandler.Print("LoadDDS: '%s' DX10 header read failed\n", name);
			return false;
		}
		const uint32_t dxgiFormat = ReadU32(header + kDDSHeaderSize);
		hasColorEncoding = true;
		if (dxgiFormat == kDXGI_BC1_UNORM)
			format = GfxPixelFormat::BC1_UNorm;
		else if (dxgiFormat == kDXGI_BC1_UNORM_SRGB) {
			format = GfxPixelFormat::BC1_UNorm_SRGB;
			colorEncoding = ecSRGB;
		}
		else if (dxgiFormat == kDXGI_BC7_UNORM)
			format = GfxPixelFormat::BC7_UNorm;
		else if (dxgiFormat == kDXGI_BC7_UNORM_SRGB) {
			format = GfxPixelFormat::BC7_UNorm_SRGB;
			colorEncoding = ecSRGB;
		}
		else if (dxgiFormat == kDXGI_BC4_UNORM)
			format = GfxPixelFormat::BC4_UNorm;
		else if (dxgiFormat == kDXGI_BC5_UNORM)
			format = GfxPixelFormat::BC5_UNorm;
		else {
			logHandler.Print("LoadDDS: '%s' has unsupported DXGI format %u (need BC1/BC4/BC5/BC7)\n", name, dxgiFormat);
			return false;
		}
	}
	else {
		logHandler.Print("LoadDDS: '%s' has unsupported FourCC 0x%08X (need DXT1 or DX10 BC7)\n", name, fourCC);
		return false;
	}

	uint32_t mipCount = (mipMapCnt > 0) ? mipMapCnt : 1u;
	if (mipCount > kMaxMipLevels)
		mipCount = kMaxMipLevels;

	const uint32_t blockBytes = GfxBlockBytes(format);

	// Total payload = sum over mip levels of ceil(w/4) * ceil(h/4) * blockBytes.
	size_t		expected = 0;
	uint32_t	w = width, h = height;
	for (uint32_t i = 0; i < mipCount; ++i) {
		expected += size_t(BlocksAcross(w)) * size_t(BlocksAcross(h)) * blockBytes;
		w = (w > 1u) ? (w >> 1) : 1u;
		h = (h > 1u) ? (h >> 1) : 1u;
	}

	const size_t payloadOffset = kDDSHeaderSize + extraHeader;
	const size_t available = (fileSize > payloadOffset) ? fileSize - payloadOffset : 0;
	if (available < expected) {
		logHandler.Print("LoadDDS: '%s' payload too small (%llu < %llu bytes)\n",
						 name, (unsigned long long)available, (unsigned long long)expected);
		return false;
	}

	layout.format = format;
	layout.width = width;
	layout.height = height;
	layout.mipCount = mipCount;
	layout.payloadOffset = payloadOffset;
	layout.payloadSize = expected;
	layout.colorEncoding = colorEncoding;
	layout.hasColorEncoding = hasColorEncoding;
	return true;
}

// Keep exactly the expected mip-chain bytes; ignore any trailing padding the encoder may add.
bool SetDDSBuffer(const char* name, const DDSLayout& layout, TextureBuffer& buf)
noexcept
{
	buf.m_info.m_width = int32_t(layout.width);
	buf.m_info.m_height = int32_t(layout.height);
	buf.m_info.m_componentCount = 0; // not applicable to block-compressed data
	buf.m_info.m_internalFormat = 0;
	buf.m_info.m_format = 0;
	buf.m_info.m_gfxFormat = layout.format;
	buf.m_info.m_mipCount = int32_t(layout.mipCount);
	buf.m_info.m_dataSize = int32_t(layout.payloadSize);
	buf.m_info.m_colorEncoding = layout.colorEncoding;
	buf.m_info.m_hasColorEncoding = layout.hasColorEncoding and useFileColorEncoding;

	buf.m_data.Resize(uint32_t(layout.payloadSize));
	if (uint32_t(buf.m_data.Length()) < uint32_t(layout.payloadSize)) {
		logHandler.Print("LoadDDS: '%s' out of memory for %llu bytes\n", name, (unsigned long long)layout.payloadSize);
		return false;
	}
	return true;
}

} // namespace


bool LoadDDS(const String& path, TextureBuffer& buf)
noexcept
{
	const char* name = (const char*)path;

	std::ifstream f(name, std::ios::binary | std::ios::ate);
	if (not f.is_open()) {
		logHandler.Print("LoadDDS: cannot open '%s'\n", name);
		return false;
	}
	const std::streamoff fileSize = f.tellg();
	if (fileSize < std::streamoff(kDDSHeaderSize)) {
		logHandler.Print("LoadDDS: '%s' is too small to be a DDS file\n", name);
		return false;
	}
	f.seekg(0, std::ios::beg);

	// Magic + fixed 124-byte header, and the DX10 header where the file is long enough to hold one.
	uint8_t			header[kDDSHeaderSize + kDDSHeaderDX10Size];
	const size_t	headerSize = (fileSize < std::streamoff(sizeof(header))) ? size_t(fileSize) : sizeof(header);
	f.read(reinterpret_cast<char*>(header), std::streamsize(headerSize));
	if (not f) {
		logHandler.Print("LoadDDS: '%s' header read failed\n", name);
		return false;
	}

	DDSLayout layout;
	if (not (ParseDDSHeader(name, header, headerSize, size_t(fileSize), layout) and SetDDSBuffer(name, layout, buf)))
		return false;

	f.seekg(std::streamoff(layout.payloadOffset), std::ios::beg);
	f.read(reinterpret_cast<char*>(buf.m_data.Data()), std::streamsize(layout.payloadSize));
	if (not f) {
		logHandler.Print("LoadDDS: '%s' payload read failed\n", name);
		return false;
	}

	return true;
}


bool LoadDDS(const uint8_t* data, size_t size, const char* name, TextureBuffer& buf)
noexcept
{
	if (size < kDDSHeaderSize) {
		logHandler.Print("LoadDDS: '%s' is too small to be a DDS file\n", name);
		return false;
	}

	DDSLayout		layout;
	const size_t	headerSize = (size < kDDSHeaderSize + kDDSHeaderDX10Size) ? size : kDDSHeaderSize + kDDSHeaderDX10Size;
	if (not (ParseDDSHeader(name, data, headerSize, size, layout) and SetDDSBuffer(name, layout, buf)))
		return false;
	std::memcpy(buf.m_data.Data(), data + layout.payloadOffset, layout.payloadSize);
	return true;
}

// =================================================================================================
// Unified file loading with automatic DDS preference. One code path for all three backends.

namespace {

// Join folder + name with exactly one separator (folder may or may not already end in a slash).
std::string JoinPath(const char* folder, const std::string& name)
{
	std::string f = folder ? folder : "";
	if (f.empty())
		return name;
	const char last = f.back();
	if ((last == '/') or (last == '\\'))
		return f + name;
	return f + "/" + name;
}

// If a sibling "<base>.dds" exists next to fileName, return its name; otherwise return fileName.
// Only the extension is swapped; any directory part of the name is preserved.
std::string PreferDDSName(const char* folder, const std::string& fileName)
{
	const size_t dot = fileName.find_last_of('.');
	const size_t slash = fileName.find_last_of("/\\");
	if ((dot == std::string::npos) or ((slash != std::string::npos) and (dot < slash)))
		return fileName; // no extension to swap
	const std::string	base = fileName.substr(0, dot);
	std::error_code		ec;
	// Both spellings: on a case sensitive file system only the one that is actually on disk is found,
	// and which of the two that is depends on whoever converted the textures.
	for (const char* ext : { ".DDS", ".dds" }) {
		const std::string ddsName = base + ext;
		if (std::filesystem::exists(JoinPath(folder, ddsName), ec))
			return ddsName;
	}
	return fileName;
}

void ReadPNGColorEncoding(const std::string& path, TextureBuffer::BufferInfo& info)
noexcept
{
	if (not useFileColorEncoding)
		return;
	std::ifstream f(path, std::ios::binary);
	if (not f.is_open())
		return;
	static constexpr uint8_t	signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	uint8_t						head[8];
	f.read(reinterpret_cast<char*>(head), std::streamsize(sizeof(head)));
	if (not f or (std::memcmp(head, signature, sizeof(signature)) != 0))
		return;
	uint8_t	chunk[8];
	bool	hasOtherColorSpace = false;
	while (f.read(reinterpret_cast<char*>(chunk), std::streamsize(sizeof(chunk)))) {
		const uint32_t length = (uint32_t(chunk[0]) << 24) | (uint32_t(chunk[1]) << 16) | (uint32_t(chunk[2]) << 8) | uint32_t(chunk[3]);
		if (std::memcmp(chunk + 4, "sRGB", 4) == 0) {
			info.m_colorEncoding = ecSRGB;
			info.m_hasColorEncoding = true;
			return;
		}
		if ((std::memcmp(chunk + 4, "iCCP", 4) == 0) or (std::memcmp(chunk + 4, "gAMA", 4) == 0) or
			(std::memcmp(chunk + 4, "cHRM", 4) == 0))
			hasOtherColorSpace = true;
		if ((std::memcmp(chunk + 4, "IDAT", 4) == 0) or (std::memcmp(chunk + 4, "IEND", 4) == 0)) {
			if (not hasOtherColorSpace) {
				info.m_colorEncoding = ecSRGB;
				info.m_hasColorEncoding = true;
			}
			return;
		}
		f.seekg(std::streamoff(length) + 4, std::ios::cur);
	}
}

} // namespace


namespace {

constexpr uint8_t kBC7Weights2[4] = { 0, 21, 43, 64 };
constexpr uint8_t kBC7Weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
constexpr uint8_t kBC7Weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

constexpr uint16_t kBC7Partitions[64] = {
	0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80, 0xE800, 0xFFE8, 0xFF00, 0xFFF0, 0xF000,
	0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310, 0x3100, 0x8CCE, 0x088C, 0x3110, 0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C,
	0xAAAA, 0xF0F0, 0x5A5A, 0x33CC, 0x3C3C, 0x55AA, 0x9696, 0xA55A, 0x73CE, 0x13C8, 0x324C, 0x3BDC, 0x6996, 0xC33C, 0x9966, 0x0660,
	0x0272, 0x04E4, 0x4E40, 0x2720, 0xC936, 0x936C, 0x39C6, 0x639C, 0x9336, 0x9CC6, 0x817E, 0xE718, 0xCCF0, 0x0FCC, 0x7744, 0xEE22
};

constexpr uint8_t kBC7Anchors[64] = {
	15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
	15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
	15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6,
	6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15
};

class BC7BitReader {
public:
	explicit BC7BitReader(const uint8_t* block)
	noexcept
		: m_block(block)
	{
	}

	uint32_t Read(uint32_t count)
	noexcept
	{
		uint32_t value = 0;
		for (uint32_t i = 0; i < count; ++i, ++m_position)
			value |= uint32_t((m_block[m_position >> 3] >> (m_position & 7u)) & 1u) << i;
		return value;
	}

private:
	const uint8_t*	m_block;
	uint32_t		m_position{ 0 };
};

inline uint32_t BC7Expand(uint32_t value, uint32_t bits)
noexcept
{
	return (value << (8u - bits)) | (value >> (2u * bits - 8u));
}

inline uint8_t BC7Interpolate(uint32_t end0, uint32_t end1, uint32_t weight)
noexcept
{
	return uint8_t(((64u - weight) * end0 + weight * end1 + 32u) >> 6);
}

inline const uint8_t* BC7Weights(uint32_t bits)
noexcept
{
	return (bits == 2u) ? kBC7Weights2 : (bits == 3u) ? kBC7Weights3 : kBC7Weights4;
}

void ReadBC7Indices(BC7BitReader& reader, uint32_t bits, uint8_t* indices)
noexcept
{
	for (uint32_t i = 0; i < 16u; ++i)
		indices[i] = uint8_t(reader.Read((i == 0u) ? bits - 1u : bits));
}

void DecodeBC7SingleSubsetAlpha(const uint8_t* block, uint32_t mode, uint8_t* alpha)
noexcept
{
	BC7BitReader	reader(block);
	reader.Read(mode + 1u);
	const uint32_t	rotation = (mode == 6u) ? 0u : reader.Read(2u);
	const bool		swapsIndices = (mode == 4u) and (reader.Read(1u) != 0u);
	uint32_t		colorBits = (mode == 4u) ? 5u : 7u;
	uint32_t		alphaBits = (mode == 4u) ? 6u : (mode == 5u) ? 8u : 7u;
	uint32_t		colorEnds[3][2];
	uint32_t		alphaEnds[2];
	for (auto& channel : colorEnds) {
		channel[0] = reader.Read(colorBits);
		channel[1] = reader.Read(colorBits);
	}
	alphaEnds[0] = reader.Read(alphaBits);
	alphaEnds[1] = reader.Read(alphaBits);
	if (mode == 6u) {
		const uint32_t p0 = reader.Read(1u);
		const uint32_t p1 = reader.Read(1u);
		for (auto& channel : colorEnds) {
			channel[0] = (channel[0] << 1) | p0;
			channel[1] = (channel[1] << 1) | p1;
		}
		alphaEnds[0] = (alphaEnds[0] << 1) | p0;
		alphaEnds[1] = (alphaEnds[1] << 1) | p1;
		colorBits = 8u;
		alphaBits = 8u;
	}
	for (auto& channel : colorEnds) {
		channel[0] = BC7Expand(channel[0], colorBits);
		channel[1] = BC7Expand(channel[1], colorBits);
	}
	alphaEnds[0] = BC7Expand(alphaEnds[0], alphaBits);
	alphaEnds[1] = BC7Expand(alphaEnds[1], alphaBits);
	uint8_t first[16];
	uint8_t second[16];
	if (mode == 6u) {
		ReadBC7Indices(reader, 4u, first);
		for (uint32_t i = 0; i < 16u; ++i)
			alpha[i] = BC7Interpolate(alphaEnds[0], alphaEnds[1], kBC7Weights4[first[i]]);
		return;
	}
	const uint32_t secondBits = (mode == 4u) ? 3u : 2u;
	ReadBC7Indices(reader, 2u, first);
	ReadBC7Indices(reader, secondBits, second);
	if (rotation == 0u) {
		const uint8_t* indices = swapsIndices ? first : second;
		const uint8_t* weights = BC7Weights(swapsIndices ? 2u : secondBits);
		for (uint32_t i = 0; i < 16u; ++i)
			alpha[i] = BC7Interpolate(alphaEnds[0], alphaEnds[1], weights[indices[i]]);
		return;
	}
	const uint8_t*	indices = swapsIndices ? second : first;
	const uint8_t*	weights = BC7Weights(swapsIndices ? secondBits : 2u);
	const uint32_t*	channel = colorEnds[rotation - 1u];
	for (uint32_t i = 0; i < 16u; ++i)
		alpha[i] = BC7Interpolate(channel[0], channel[1], weights[indices[i]]);
}

void DecodeBC7TwoSubsetAlpha(const uint8_t* block, uint8_t* alpha)
noexcept
{
	BC7BitReader reader(block);
	reader.Read(8u);
	const uint32_t partition = reader.Read(6u);
	for (uint32_t i = 0; i < 12u; ++i)
		reader.Read(5u);
	uint32_t ends[4];
	for (auto& end : ends)
		end = reader.Read(5u);
	for (auto& end : ends)
		end = BC7Expand((end << 1) | reader.Read(1u), 6u);
	const uint32_t anchor = kBC7Anchors[partition];
	for (uint32_t texel = 0; texel < 16u; ++texel) {
		const uint32_t index = reader.Read(((texel == 0u) or (texel == anchor)) ? 1u : 2u);
		const uint32_t subset = (uint32_t(kBC7Partitions[partition]) >> texel) & 1u;
		alpha[texel] = BC7Interpolate(ends[2u * subset], ends[2u * subset + 1u], kBC7Weights2[index]);
	}
}

void DecodeBC7Alpha(const uint8_t* block, uint8_t* alpha)
noexcept
{
	uint32_t mode = 0;
	while ((mode < 8u) and (((block[0] >> mode) & 1u) == 0u))
		++mode;
	if (mode == 7u)
		DecodeBC7TwoSubsetAlpha(block, alpha);
	else if ((mode >= 4u) and (mode <= 6u))
		DecodeBC7SingleSubsetAlpha(block, mode, alpha);
	else
		std::memset(alpha, 255, 16);
}

constexpr uint8_t kBC7Partitions3[64][16] = {
	{ 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2 },
	{ 0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1 },
	{ 0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1 },
	{ 0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2 },
	{ 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2 },
	{ 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2 },
	{ 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2 },
	{ 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2 },
	{ 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2 },
	{ 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2 },
	{ 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0 },
	{ 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2 },
	{ 0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2 },
	{ 0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1 },
	{ 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2 },
	{ 0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1 },
	{ 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2 },
	{ 0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0 },
	{ 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2 },
	{ 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1 },
	{ 0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2 },
	{ 0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2 },
	{ 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1 },
	{ 0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2 },
	{ 0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1 },
	{ 0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2 },
	{ 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0 },
	{ 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0 },
	{ 0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0 },
	{ 0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1 },
	{ 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1 },
	{ 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1 },
	{ 0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2 },
	{ 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1 },
	{ 0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1 },
	{ 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1 },
	{ 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1 },
	{ 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2 },
	{ 0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1 },
	{ 0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2 },
	{ 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2 },
	{ 0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2 },
	{ 0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2 },
	{ 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2 },
	{ 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2 },
	{ 0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2 },
	{ 0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2 },
	{ 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1 },
	{ 0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2 },
	{ 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
	{ 0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0 }
};

constexpr uint8_t kBC7Anchors3Second[64] = {
	3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3,
	3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
	8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15,
	3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3
};

constexpr uint8_t kBC7Anchors3Third[64] = {
	15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8,
	15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
	15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8,
	15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8
};

struct BC7Mode {
	uint8_t subsets;
	uint8_t partitionBits;
	uint8_t rotationBits;
	uint8_t indexModeBits;
	uint8_t colorBits;
	uint8_t alphaBits;
	uint8_t endpointPBits;
	uint8_t sharedPBits;
	uint8_t indexBits;
	uint8_t index2Bits;
};

constexpr BC7Mode kBC7Modes[8] = {
	{ 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
	{ 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
	{ 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
	{ 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
	{ 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
	{ 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
	{ 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
	{ 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 }
};

inline uint32_t BC7Subset(uint32_t subsets, uint32_t partition, uint32_t texel)
noexcept
{
	if (subsets == 2u)
		return (uint32_t(kBC7Partitions[partition]) >> texel) & 1u;
	if (subsets == 3u)
		return kBC7Partitions3[partition][texel];
	return 0u;
}

inline bool BC7IsAnchor(uint32_t subsets, uint32_t partition, uint32_t texel)
noexcept
{
	if (texel == 0u)
		return true;
	if (subsets == 2u)
		return texel == kBC7Anchors[partition];
	if (subsets == 3u)
		return (texel == kBC7Anchors3Second[partition]) or (texel == kBC7Anchors3Third[partition]);
	return false;
}

void DecodeBC7Block(const uint8_t* block, uint8_t* rgba)
noexcept
{
	uint32_t mode = 0;
	while ((mode < 8u) and (((block[0] >> mode) & 1u) == 0u))
		++mode;
	if (mode == 8u) {
		std::memset(rgba, 0, 64);
		return;
	}
	const BC7Mode&	m = kBC7Modes[mode];
	BC7BitReader	reader(block);
	reader.Read(mode + 1u);
	const uint32_t	partition = reader.Read(m.partitionBits);
	const uint32_t	rotation = reader.Read(m.rotationBits);
	const uint32_t	indexMode = reader.Read(m.indexModeBits);
	const uint32_t	endpoints = 2u * m.subsets;
	uint32_t		ends[6][4];
	uint32_t		colorBits = m.colorBits;
	uint32_t		alphaBits = m.alphaBits;
	for (uint32_t c = 0; c < 3u; ++c)
		for (uint32_t e = 0; e < endpoints; ++e)
			ends[e][c] = reader.Read(colorBits);
	for (uint32_t e = 0; e < endpoints; ++e)
		ends[e][3] = alphaBits ? reader.Read(alphaBits) : 255u;
	if (m.endpointPBits) {
		for (uint32_t e = 0; e < endpoints; ++e) {
			const uint32_t p = reader.Read(1u);
			for (uint32_t c = 0; c < 3u; ++c)
				ends[e][c] = (ends[e][c] << 1) | p;
			if (alphaBits)
				ends[e][3] = (ends[e][3] << 1) | p;
		}
		++colorBits;
		if (alphaBits)
			++alphaBits;
	}
	else if (m.sharedPBits) {
		for (uint32_t s = 0; s < m.subsets; ++s) {
			const uint32_t p = reader.Read(1u);
			for (uint32_t e = 2u * s; e < 2u * s + 2u; ++e)
				for (uint32_t c = 0; c < 3u; ++c)
					ends[e][c] = (ends[e][c] << 1) | p;
		}
		++colorBits;
	}
	for (uint32_t e = 0; e < endpoints; ++e) {
		for (uint32_t c = 0; c < 3u; ++c)
			ends[e][c] = BC7Expand(ends[e][c], colorBits);
		if (alphaBits)
			ends[e][3] = BC7Expand(ends[e][3], alphaBits);
	}
	uint8_t indices[16];
	uint8_t indices2[16];
	for (uint32_t texel = 0; texel < 16u; ++texel)
		indices[texel] = uint8_t(reader.Read(BC7IsAnchor(m.subsets, partition, texel) ? m.indexBits - 1u : m.indexBits));
	if (m.index2Bits)
		for (uint32_t texel = 0; texel < 16u; ++texel)
			indices2[texel] = uint8_t(reader.Read((texel == 0u) ? m.index2Bits - 1u : m.index2Bits));
	const uint8_t* weights = BC7Weights(m.indexBits);
	const uint8_t* weights2 = BC7Weights(m.index2Bits);
	for (uint32_t texel = 0; texel < 16u; ++texel) {
		const uint32_t	subset = BC7Subset(m.subsets, partition, texel);
		const uint32_t*	end0 = ends[2u * subset];
		const uint32_t*	end1 = ends[2u * subset + 1u];
		uint32_t		colorWeight = weights[indices[texel]];
		uint32_t		alphaWeight = colorWeight;
		if (m.index2Bits) {
			if (indexMode) {
				colorWeight = weights2[indices2[texel]];
				alphaWeight = weights[indices[texel]];
			}
			else
				alphaWeight = weights2[indices2[texel]];
		}
		uint8_t* texelRGBA = rgba + 4u * texel;
		for (uint32_t c = 0; c < 3u; ++c)
			texelRGBA[c] = BC7Interpolate(end0[c], end1[c], colorWeight);
		texelRGBA[3] = BC7Interpolate(end0[3], end1[3], alphaWeight);
		if (rotation) {
			const uint8_t swap = texelRGBA[3];
			texelRGBA[3] = texelRGBA[rotation - 1u];
			texelRGBA[rotation - 1u] = swap;
		}
	}
}

void DecodeBC4Block(const uint8_t* block, uint8_t* values)
noexcept
{
	const uint32_t	r0 = block[0];
	const uint32_t	r1 = block[1];
	uint8_t			palette[8];
	palette[0] = uint8_t(r0);
	palette[1] = uint8_t(r1);
	if (r0 > r1) {
		for (uint32_t i = 1; i < 7u; ++i)
			palette[i + 1u] = uint8_t(((7u - i) * r0 + i * r1) / 7u);
	}
	else {
		for (uint32_t i = 1; i < 5u; ++i)
			palette[i + 1u] = uint8_t(((5u - i) * r0 + i * r1) / 5u);
		palette[6] = 0;
		palette[7] = 255;
	}
	uint64_t bits = 0;
	for (uint32_t i = 0; i < 6u; ++i)
		bits |= uint64_t(block[2u + i]) << (8u * i);
	for (uint32_t texel = 0; texel < 16u; ++texel)
		values[texel] = palette[(bits >> (3u * texel)) & 7u];
}

} // namespace


TextureBuffer* LoadTextureFile(const String& folder, const String& fileName,
							   bool premultiply, bool flipVertically, bool isRequired,
							   bool allowDDS)
noexcept
{
	const char*	folderC = (const char*)folder;
	std::string	wanted = (const char*)fileName;
	const bool	haveBlockCompression = baseRenderer.HasFeature(GfxFeature::BlockCompression);
	if (allowDDS and haveBlockCompression)
		wanted = PreferDDSName(folderC, wanted);

	const std::string	full = JoinPath(folderC, wanted);
	const String		fullPath(full.c_str());

	if (IsDDSFile(fullPath) and not haveBlockCompression) {
		logHandler.Print("LoadTextureFile: '%s' is block compressed, which is not enabled on this device\n", full.c_str());
		return nullptr;
	}

	TextureBuffer* buf = new TextureBuffer();

	if (IsDDSFile(fullPath)) {
		if (not LoadDDS(fullPath, *buf)) {
			if (isRequired)
				missingFiles.Report(full.c_str());
			delete buf;
			return nullptr;
		}
		return buf;
	}

	SDL_Surface* image = IMG_Load(full.c_str());
	if (not image) {
		if (isRequired) {
			logHandler.Print("LoadTextureFile: failed to load '%s'\n", full.c_str());
			missingFiles.Report(full.c_str());
		}
		delete buf;
		return nullptr;
	}
	buf->Create(image, premultiply, flipVertically);
	ReadPNGColorEncoding(full, buf->m_info);
	return buf;
}


TextureBuffer* LoadTextureMemory(const uint8_t* data, size_t size, const char* name)
noexcept
{
	TextureBuffer* buf = new TextureBuffer();

	if ((size >= 4) and (ReadU32(data) == kDDSMagic)) {
		if (not LoadDDS(data, size, name, *buf)) {
			delete buf;
			return nullptr;
		}
		return buf;
	}

	SDL_IOStream*	stream = SDL_IOFromConstMem(data, size);
	SDL_Surface*	image = stream ? IMG_Load_IO(stream, true) : nullptr;
	if (not image) {
		logHandler.Print("LoadTextureMemory: failed to load '%s'\n", name);
		delete buf;
		return nullptr;
	}
	buf->Create(image, false, false);
	return buf;
}


bool HasCompressedImage(const String& imageName)
noexcept
{
	if (imageName.IsEmpty() or not baseRenderer.HasFeature(GfxFeature::BlockCompression))
		return false;
	return IsDDSFile(String(PreferDDSName(nullptr, static_cast<const char*>(imageName)).c_str()));
}


bool LoadCompressedAlpha(const String& imageName, AutoArray<uint8_t>& alpha, int32_t& width, int32_t& height)
noexcept
{
	const String	path(PreferDDSName(nullptr, static_cast<const char*>(imageName)).c_str());
	TextureBuffer	buffer;
	if (not LoadDDS(path, buffer))
		return false;
	const GfxPixelFormat format = buffer.m_info.m_gfxFormat;
	if ((format != GfxPixelFormat::BC7_UNorm) and (format != GfxPixelFormat::BC7_UNorm_SRGB)) {
		logHandler.Print("LoadCompressedAlpha: '%s' is not BC7\n", static_cast<const char*>(path));
		return false;
	}
	width = buffer.m_info.m_width;
	height = buffer.m_info.m_height;
	alpha.Resize(width * height);
	const uint8_t*	block = buffer.m_data.Data();
	const int32_t	blocksAcross = int32_t(BlocksAcross(uint32_t(width)));
	const int32_t	blocksDown = int32_t(BlocksAcross(uint32_t(height)));
	for (int32_t y = 0; y < blocksDown; ++y) {
		for (int32_t x = 0; x < blocksAcross; ++x, block += 16) {
			uint8_t texels[16];
			DecodeBC7Alpha(block, texels);
			for (int32_t j = 0, row = y * 4; (j < 4) and (row < height); ++j, ++row) {
				for (int32_t i = 0, column = x * 4; (i < 4) and (column < width); ++i, ++column)
					alpha[row * width + column] = texels[j * 4 + i];
			}
		}
	}
	return true;
}


bool DecodeCompressedRows(const TextureBuffer& buf, int32_t firstBlockRow, int32_t blockRows, uint8_t* rgba)
noexcept
{
	const GfxPixelFormat	format = buf.m_info.m_gfxFormat;
	const bool				isBC7 = (format == GfxPixelFormat::BC7_UNorm) or (format == GfxPixelFormat::BC7_UNorm_SRGB);
	if (not (isBC7 or (format == GfxPixelFormat::BC4_UNorm)))
		return false;
	const int32_t	width = buf.m_info.m_width;
	const int32_t	height = buf.m_info.m_height;
	const int32_t	blocksAcross = int32_t(BlocksAcross(uint32_t(width)));
	const int32_t	blocksDown = int32_t(BlocksAcross(uint32_t(height)));
	const size_t	blockBytes = GfxBlockBytes(format);
	const int32_t	lastBlockRow = (firstBlockRow + blockRows < blocksDown) ? firstBlockRow + blockRows : blocksDown;
	for (int32_t y = (firstBlockRow > 0) ? firstBlockRow : 0; y < lastBlockRow; ++y) {
		const uint8_t* block = buf.m_data.Data() + size_t(y) * size_t(blocksAcross) * blockBytes;
		for (int32_t x = 0; x < blocksAcross; ++x, block += blockBytes) {
			uint8_t texels[64];
			if (isBC7)
				DecodeBC7Block(block, texels);
			else {
				uint8_t values[16];
				DecodeBC4Block(block, values);
				for (int32_t i = 0; i < 16; ++i) {
					texels[4 * i] = texels[4 * i + 1] = texels[4 * i + 2] = values[i];
					texels[4 * i + 3] = 255;
				}
			}
			for (int32_t j = 0, row = y * 4; (j < 4) and (row < height); ++j, ++row) {
				for (int32_t i = 0, column = x * 4; (i < 4) and (column < width); ++i, ++column)
					std::memcpy(rgba + (size_t(row) * size_t(width) + size_t(column)) * 4, texels + (j * 4 + i) * 4, 4);
			}
		}
	}
	return true;
}


bool ReadPNGText(const String& path, const char* keyword, std::string& text)
noexcept
{
	std::ifstream f(static_cast<const char*>(path), std::ios::binary);
	if (not f.is_open())
		return false;
	static constexpr uint8_t	signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	uint8_t						head[8];
	f.read(reinterpret_cast<char*>(head), std::streamsize(sizeof(head)));
	if (not f or (std::memcmp(head, signature, sizeof(signature)) != 0))
		return false;
	const size_t	keyLength = std::strlen(keyword);
	uint8_t			chunk[8];
	while (f.read(reinterpret_cast<char*>(chunk), std::streamsize(sizeof(chunk)))) {
		const uint32_t length = (uint32_t(chunk[0]) << 24) | (uint32_t(chunk[1]) << 16) | (uint32_t(chunk[2]) << 8) | uint32_t(chunk[3]);
		if (std::memcmp(chunk + 4, "IEND", 4) == 0)
			return false;
		if ((std::memcmp(chunk + 4, "tEXt", 4) == 0) and (length > keyLength)) {
			std::string data(length, '\0');
			if (not f.read(data.data(), std::streamsize(length)))
				return false;
			if ((std::memcmp(data.data(), keyword, keyLength) == 0) and (data[keyLength] == '\0')) {
				text = data.substr(keyLength + 1);
				return true;
			}
			f.seekg(4, std::ios::cur);
			continue;
		}
		f.seekg(std::streamoff(length) + 4, std::ios::cur);
	}
	return false;
}


static bool IsTGAFile(const char* name)
noexcept
{
	const size_t n = std::strlen(name);
	if (n < 4)
		return false;
	const char*	t = name + (n - 4);
	auto		lower = [](char c) noexcept { return ((c >= 'A') and (c <= 'Z')) ? char(c - 'A' + 'a') : c; };
	return (t[0] == '.') and (lower(t[1]) == 't') and (lower(t[2]) == 'g') and (lower(t[3]) == 'a');
}


bool ReadImageExtent(const String& path, int32_t& width, int32_t& height)
noexcept
{
	const char* name = static_cast<const char*>(path);
	if (name == nullptr)
		return false;
	std::ifstream f(name, std::ios::binary);
	if (not f.is_open())
		return false;
	static constexpr uint8_t	signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	uint8_t						head[24];
	f.read(reinterpret_cast<char*>(head), std::streamsize(sizeof(head)));
	const std::streamsize length = f.gcount();
	if ((length >= std::streamsize(sizeof(head))) and (std::memcmp(head, signature, sizeof(signature)) == 0) and (std::memcmp(head + 12, "IHDR", 4) == 0)) {
		width = int32_t((uint32_t(head[16]) << 24) | (uint32_t(head[17]) << 16) | (uint32_t(head[18]) << 8) | uint32_t(head[19]));
		height = int32_t((uint32_t(head[20]) << 24) | (uint32_t(head[21]) << 16) | (uint32_t(head[22]) << 8) | uint32_t(head[23]));
		return true;
	}
	if ((length >= std::streamsize(4 + kOffWidth + 4)) and (ReadU32(head) == kDDSMagic)) {
		height = int32_t(ReadU32(head + 4 + kOffHeight));
		width = int32_t(ReadU32(head + 4 + kOffWidth));
		return true;
	}
	if ((length >= 16) and IsTGAFile(name)) {
		width = int32_t(head[12]) | (int32_t(head[13]) << 8);
		height = int32_t(head[14]) | (int32_t(head[15]) << 8);
		return true;
	}
	return false;
}

// =================================================================================================
