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

bool BaseTextureArray::CreateSlots(String name, int slotWidth, int slotHeight, int slotCount, int components) {
	DestroySlots();
	if ((slotWidth < 1) or (slotHeight < 1) or (slotCount < 1) or (components < 1) or (components > 4))
		return false;
	try {
		m_pixels.Resize(slotWidth * slotHeight * components * slotCount);
	}
	catch (...) {
		return false;
	}
	if (m_pixels.Length() < slotWidth * slotHeight * components * slotCount)
		return false;
	m_pixels.Clear(0);
	m_arrayName = name;
	m_slotWidth = slotWidth;
	m_slotHeight = slotHeight;
	m_slotCount = slotCount;
	m_components = components;
	return true;
}

// -------------------------------------------------------------------------------------------------

void BaseTextureArray::DestroySlots(void) {
	m_pixels.Reset();
	m_slotWidth = m_slotHeight = m_slotCount = 0;
}

// -------------------------------------------------------------------------------------------------
// Bilinear magnification. The sample positions map the DESTINATION's pixel centres back into the
// source ((x + 0.5) * srcW / dstW - 0.5), not its pixel corners: with corners the image drifts half a
// destination pixel towards the origin, and on a sprite sheet that drift is exactly what puts a cell's
// last column into the next cell.

void BaseTextureArray::ScaleIntoSlot(uint8_t* dst, const uint8_t* src, int srcWidth, int srcHeight) {
	const float scaleX = float(srcWidth) / float(m_slotWidth);
	const float scaleY = float(srcHeight) / float(m_slotHeight);
	const int   srcStride = srcWidth * m_components;

	for (int y = 0; y < m_slotHeight; y++) {
		float   fy = (float(y) + 0.5f) * scaleY - 0.5f;
		if (fy < 0.0f)
			fy = 0.0f;
		int     y0 = int(fy);
		int     y1 = (y0 + 1 < srcHeight) ? y0 + 1 : srcHeight - 1;
		float   wy = fy - float(y0);
		uint8_t* dstRow = dst + size_t(y) * size_t(m_slotWidth) * size_t(m_components);

		for (int x = 0; x < m_slotWidth; x++) {
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

bool BaseTextureArray::SetSlot(int slotIndex, const uint8_t* data, int width, int height, int components) {
	if (not HasSlots() or (slotIndex < 0) or (slotIndex >= m_slotCount) or (data == nullptr))
		return false;
	// A different component count would need a channel conversion, which is a different job from
	// scaling and has no single right answer (what does an RGB image put in the alpha channel?).
	// The caller decides that before it gets here.
	if (components != m_components)
		return false;
	if ((width < 1) or (height < 1) or (width > m_slotWidth) or (height > m_slotHeight))
		return false;

	uint8_t* dst = m_pixels.DataPtr() + size_t(slotIndex) * size_t(SlotSize());

	if ((width == m_slotWidth) and (height == m_slotHeight))
		memcpy(dst, data, size_t(SlotSize()));
	else
		ScaleIntoSlot(dst, data, width, height);
	return true;
}

// -------------------------------------------------------------------------------------------------

int BaseTextureArray::MipCount(bool useMipMaps) const noexcept {
	if (not useMipMaps)
		return 1;
	int n = 1;
	int d = (m_slotWidth > m_slotHeight) ? m_slotWidth : m_slotHeight;
	while (d > 1) {
		d >>= 1;
		n++;
	}
	return n;
}

// -------------------------------------------------------------------------------------------------
// 2x2 box filter, one chain per slot. Odd dimensions are covered by clamping the second sample to
// the last row or column, so a 1 pixel wide level averages that one pixel with itself instead of
// reading past the end.

bool BaseTextureArray::BuildMipChains(int mipCount, AutoArray<uint8_t>& chains, AutoArray<const uint8_t*>& slotPtrs) {
	if (not HasSlots() or (mipCount < 1))
		return false;

	// How long one slot's chain is.
	size_t chainSize = 0;
	{
		int w = m_slotWidth, h = m_slotHeight;
		for (int mip = 0; mip < mipCount; mip++) {
			chainSize += size_t(w) * size_t(h) * size_t(m_components);
			w = (w > 1) ? (w >> 1) : 1;
			h = (h > 1) ? (h >> 1) : 1;
		}
	}

	try {
		chains.Resize(int32_t(chainSize * size_t(m_slotCount)));
		slotPtrs.Resize(m_slotCount);
	}
	catch (...) {
		return false;
	}
	if ((chains.Length() < int32_t(chainSize * size_t(m_slotCount))) or (slotPtrs.Length() < m_slotCount))
		return false;

	for (int slot = 0; slot < m_slotCount; slot++) {
		uint8_t* chain = chains.DataPtr() + size_t(slot) * chainSize;
		slotPtrs[slot] = chain;

		memcpy(chain, SlotData(slot), size_t(SlotSize()));

		uint8_t* src = chain;
		int srcW = m_slotWidth, srcH = m_slotHeight;

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
