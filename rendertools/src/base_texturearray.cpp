#include "base_texturearray.h"

// =================================================================================================

int BaseTextureArray::NextPowerOfTwo(int n) noexcept {
	if (n < 1)
		return 1;
	int p = 1;
	while (p < n)
		p <<= 1;
	return p;
}

// -------------------------------------------------------------------------------------------------

bool BaseTextureArray::CreateLayers(String name, int layerWidth, int layerHeight, int layerCount, int components) {
	DestroyLayers();
	if ((layerWidth < 1) or (layerHeight < 1) or (layerCount < 1) or (components < 1) or (components > 4))
		return false;
	try {
		m_pixels.Resize(layerWidth * layerHeight * components * layerCount);
	}
	catch (...) {
		return false;
	}
	if (m_pixels.Length() < layerWidth * layerHeight * components * layerCount)
		return false;
	m_pixels.Clear(0);
	m_arrayName = name;
	m_layerWidth = layerWidth;
	m_layerHeight = layerHeight;
	m_layerCount = layerCount;
	m_components = components;
	return true;
}

// -------------------------------------------------------------------------------------------------

void BaseTextureArray::DestroyLayers(void) {
	m_pixels.Reset();
	m_layerWidth = m_layerHeight = m_layerCount = 0;
}

// -------------------------------------------------------------------------------------------------
// Bilinear magnification. The sample positions map the DESTINATION's pixel centres back into the
// source ((x + 0.5) * srcW / dstW - 0.5), not its pixel corners: with corners the image drifts half a
// destination pixel towards the origin, and on a sprite sheet that drift is exactly what puts a cell's
// last column into the next cell.

void BaseTextureArray::ScaleIntoLayer(uint8_t* dst, const uint8_t* src, int srcWidth, int srcHeight) {
	const float scaleX = float(srcWidth) / float(m_layerWidth);
	const float scaleY = float(srcHeight) / float(m_layerHeight);
	const int   srcStride = srcWidth * m_components;

	for (int y = 0; y < m_layerHeight; y++) {
		float   fy = (float(y) + 0.5f) * scaleY - 0.5f;
		if (fy < 0.0f)
			fy = 0.0f;
		int     y0 = int(fy);
		int     y1 = (y0 + 1 < srcHeight) ? y0 + 1 : srcHeight - 1;
		float   wy = fy - float(y0);
		uint8_t* dstRow = dst + size_t(y) * size_t(m_layerWidth) * size_t(m_components);

		for (int x = 0; x < m_layerWidth; x++) {
			float   fx = (float(x) + 0.5f) * scaleX - 0.5f;
			if (fx < 0.0f)
				fx = 0.0f;
			int     x0 = int(fx);
			int     x1 = (x0 + 1 < srcWidth) ? x0 + 1 : srcWidth - 1;
			float   wx = fx - float(x0);

			const uint8_t* p00 = src + size_t(y0) * size_t(srcStride) + size_t(x0) * size_t(m_components);
			const uint8_t* p01 = src + size_t(y0) * size_t(srcStride) + size_t(x1) * size_t(m_components);
			const uint8_t* p10 = src + size_t(y1) * size_t(srcStride) + size_t(x0) * size_t(m_components);
			const uint8_t* p11 = src + size_t(y1) * size_t(srcStride) + size_t(x1) * size_t(m_components);

			for (int c = 0; c < m_components; c++) {
				float top = float(p00[c]) + (float(p01[c]) - float(p00[c])) * wx;
				float bot = float(p10[c]) + (float(p11[c]) - float(p10[c])) * wx;
				float v = top + (bot - top) * wy;
				dstRow[size_t(x) * size_t(m_components) + size_t(c)] = uint8_t(v + 0.5f);
			}
		}
	}
}

// -------------------------------------------------------------------------------------------------

bool BaseTextureArray::SetLayer(int layerIndex, const uint8_t* data, int width, int height, int components) {
	if (not HasLayers() or (layerIndex < 0) or (layerIndex >= m_layerCount) or (data == nullptr))
		return false;
	// A different component count would need a channel conversion, which is a different job from
	// scaling and has no single right answer (what does an RGB image put in the alpha channel?).
	// The caller decides that before it gets here.
	if (components != m_components)
		return false;
	if ((width < 1) or (height < 1) or (width > m_layerWidth) or (height > m_layerHeight))
		return false;

	uint8_t* dst = m_pixels.DataPtr() + size_t(layerIndex) * size_t(LayerSize());

	if ((width == m_layerWidth) and (height == m_layerHeight))
		memcpy(dst, data, size_t(LayerSize()));
	else
		ScaleIntoLayer(dst, data, width, height);
	return true;
}

// -------------------------------------------------------------------------------------------------

int BaseTextureArray::MipCount(bool useMipMaps) const noexcept {
	if (not useMipMaps)
		return 1;
	int n = 1;
	int d = (m_layerWidth > m_layerHeight) ? m_layerWidth : m_layerHeight;
	while (d > 1) {
		d >>= 1;
		n++;
	}
	return n;
}

// -------------------------------------------------------------------------------------------------
// 2x2 box filter, one chain per layer. Odd dimensions are covered by clamping the second sample to
// the last row or column, so a 1 pixel wide level averages that one pixel with itself instead of
// reading past the end.

bool BaseTextureArray::BuildMipChains(int mipCount, AutoArray<uint8_t>& chains, AutoArray<const uint8_t*>& layerPtrs) {
	if (not HasLayers() or (mipCount < 1))
		return false;

	// How long one layer's chain is.
	size_t chainSize = 0;
	{
		int w = m_layerWidth, h = m_layerHeight;
		for (int mip = 0; mip < mipCount; mip++) {
			chainSize += size_t(w) * size_t(h) * size_t(m_components);
			w = (w > 1) ? (w >> 1) : 1;
			h = (h > 1) ? (h >> 1) : 1;
		}
	}

	try {
		chains.Resize(int32_t(chainSize * size_t(m_layerCount)));
		layerPtrs.Resize(m_layerCount);
	}
	catch (...) {
		return false;
	}
	if ((chains.Length() < int32_t(chainSize * size_t(m_layerCount))) or (layerPtrs.Length() < m_layerCount))
		return false;

	for (int layer = 0; layer < m_layerCount; layer++) {
		uint8_t* chain = chains.DataPtr() + size_t(layer) * chainSize;
		layerPtrs[layer] = chain;

		memcpy(chain, LayerData(layer), size_t(LayerSize()));

		uint8_t* src = chain;
		int srcW = m_layerWidth, srcH = m_layerHeight;

		for (int mip = 1; mip < mipCount; mip++) {
			uint8_t* dst = src + size_t(srcW) * size_t(srcH) * size_t(m_components);
			int dstW = (srcW > 1) ? (srcW >> 1) : 1;
			int dstH = (srcH > 1) ? (srcH >> 1) : 1;

			for (int y = 0; y < dstH; y++) {
				int y0 = y * 2;
				int y1 = (y0 + 1 < srcH) ? y0 + 1 : y0;
				for (int x = 0; x < dstW; x++) {
					int x0 = x * 2;
					int x1 = (x0 + 1 < srcW) ? x0 + 1 : x0;
					const uint8_t* p00 = src + (size_t(y0) * size_t(srcW) + size_t(x0)) * size_t(m_components);
					const uint8_t* p01 = src + (size_t(y0) * size_t(srcW) + size_t(x1)) * size_t(m_components);
					const uint8_t* p10 = src + (size_t(y1) * size_t(srcW) + size_t(x0)) * size_t(m_components);
					const uint8_t* p11 = src + (size_t(y1) * size_t(srcW) + size_t(x1)) * size_t(m_components);
					uint8_t* q = dst + (size_t(y) * size_t(dstW) + size_t(x)) * size_t(m_components);
					for (int c = 0; c < m_components; c++)
						q[c] = uint8_t((int(p00[c]) + int(p01[c]) + int(p10[c]) + int(p11[c]) + 2) / 4);
				}
			}
			src = dst;
			srcW = dstW;
			srcH = dstH;
		}
	}
	return true;
}

// =================================================================================================
