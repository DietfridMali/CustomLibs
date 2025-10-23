
#include <utility>
#include <stdio.h>
#include <stdexcept>
#include "texturebuffer.h"
#include "SDL_image.h"
#include "opengl_states.h"

// =================================================================================================

struct RGB8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    inline bool IsVisible(void) noexcept {
        return true;
    }
};

struct RGBA8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    inline bool IsVisible(void) noexcept {
        return a > 0;
    }
};

TextureBuffer::TextureBuffer(SDL_Surface* source, bool premultiply, bool flipVertically) {
    Create(source, premultiply, flipVertically);
}


TextureBuffer::TextureBuffer(TextureBuffer const& other) {
    Copy(const_cast<TextureBuffer&> (other));
}


TextureBuffer::TextureBuffer(TextureBuffer&& other) noexcept {
    Move(other);
}


void TextureBuffer::Reset(void)
noexcept
{
    m_info.Reset();
#if USE_SHARED_POINTERS
    m_data.Release();
#else
    m_data.Reset();
#endif
    m_isPosterized = false;
    m_isCartoonized = false;
}


void TextureBuffer::FlipSurface(SDL_Surface* source)
noexcept
{
    SDL_LockSurface(source);
    int pitch = source->pitch; // row size
    int h = source->h;
    char* pixels = (char*)source->pixels + h * pitch;
    uint8_t* dataPtr = reinterpret_cast<uint8_t*>(m_data.Data());

    for (int i = 0; i < h; i++) {
        pixels -= pitch;
        memcpy(dataPtr, pixels, pitch);
        dataPtr += pitch;
    }
    SDL_UnlockSurface(source);
}


uint8_t TextureBuffer::Premultiply(uint16_t c, uint16_t a) noexcept {
    uint8_t r = c % a > (a >> 1); // round up if c % a > a / 2
    // c = max. alpha * c / a
    c *= a;
    c += 128;
    return (uint8_t)((c + (c >> 8)) >> 8);
}


void TextureBuffer::Premultiply(void) {
    if (m_info.m_format == GL_RGBA8) {
        RGBA8* p = reinterpret_cast<RGBA8*>(m_data.Data());
        for (int i = m_info.m_dataSize / 4; i; --i, ++p) {
            uint16_t a = uint16_t (p->a);
            p->r = Premultiply(uint16_t(p->r), a);
            p->g = Premultiply(uint16_t(p->g), a);
            p->b = Premultiply(uint16_t(p->b), a);
        }
    }
}


bool TextureBuffer::Allocate(int width, int height, int componentCount, void* data) noexcept {
    m_info.m_width = width;
    m_info.m_height = height;
    m_info.m_componentCount = componentCount;
    m_info.m_format = (componentCount == 1) ? GL_RED : (componentCount == 3) ? GL_RGB : GL_RGBA;
    m_info.m_internalFormat = (componentCount == 1) ? GL_R8 : (componentCount == 3) ? GL_RGB8 : GL_RGBA8;
    m_info.m_dataSize = width * height * componentCount;
    try {
#if USE_SHARED_POINTERS
        m_data = SharedPointer<char>(m_info.m_dataSize);
#else
        m_data.Resize(m_info.m_dataSize);
#endif
    }
    catch(...) {
        return false;
    }
    if (data) {
#if USE_SHARED_POINTERS
        if (m_data.Data() == nullptr)
#else
        if (m_data.Length() < m_info.m_dataSize)
#endif
            return false;
        memcpy(m_data.Data(), data, m_info.m_dataSize);
    }
    return true;
}


TextureBuffer& TextureBuffer::Create(SDL_Surface* source, bool premultiply, bool flipVertically) {
    if (source->pitch / source->w < 3) {
        SDL_Surface* h = source;
        source = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(h);
    }
    if (not Allocate(source->w, source->h, source->pitch / source->w))
        fprintf(stderr, "%s (%d): Memory allocation for texture clone failed\n", __FILE__, __LINE__);
    else {
        if (flipVertically)
            FlipSurface(source);
        else
            memcpy(m_data.Data(), source->pixels, m_info.m_dataSize);
        SDL_FreeSurface(source);
        if (premultiply)
            Premultiply();
    }
    return *this;
}


TextureBuffer& TextureBuffer::operator= (const TextureBuffer& other)
noexcept
{
    return Copy(const_cast<TextureBuffer&>(other));
}

TextureBuffer& TextureBuffer::operator= (TextureBuffer&& other)
noexcept
{
    return Move(other);
}

TextureBuffer& TextureBuffer::Copy(TextureBuffer& other)
noexcept
{
    if (this != &other) {
        m_info = other.m_info;
        m_data = other.m_data;
    }
    return *this;
}

TextureBuffer& TextureBuffer::Move(TextureBuffer& other)
noexcept
{
    if (this != &other) {
        m_info = other.m_info;
        m_data = std::move(other.m_data);
        other.Reset();
    }
    return *this;
}


static inline uint8_t Posterize(uint16_t c, uint16_t g = 15) noexcept {
    return uint8_t(std::max<uint16_t>(0, (c / g + g / 2) * g - g));
}


template <typename T>
static void Posterize(T* colorBuffer, int bufSize, uint16_t gradients)
{
    for (; bufSize; --bufSize, ++colorBuffer) {
        if (colorBuffer->IsVisible()) {
            colorBuffer->r = Posterize((uint16_t)colorBuffer->r, gradients);
            colorBuffer->g = Posterize((uint16_t)colorBuffer->g, gradients);
            colorBuffer->b = Posterize((uint16_t)colorBuffer->b, gradients);
        }
    }
}


// create a mask where a byte is 255 for a color with alpha >= 0.5 and 0 for a color with alpha < 0.5
static void PrepareOutline(RGBA8* colorBuffer, uint8_t* maskBuffer, int bufSize)
{
    for (int i = 0; i < bufSize; ++i, ++colorBuffer, ++maskBuffer) 
        *maskBuffer = (colorBuffer->a > 127) ? 255 : 0;
}


static void ComputeOutline(RGBA8* colorBuffer, uint8_t* maskBuffer, int w, int h, int nPasses) // nPasses determines the outline's thickness
{
    int offsets[8][2] = { {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1,}, {0, 1}, {1, 1} };

    int nTag = 254, n = 0, a = 255;

    for (int nPass = 0; nPass < nPasses; ++nPass, --nTag) {
        for (int y = 0; y < h; ++y) {
            int i = y * w;
            for (int x = 0; x < w; ++x, ++i) {
                if (maskBuffer[i])
                    continue;
                for (int o = 0; o < 8; ++o) {
                    int j = std::clamp(y + offsets[o][1], 0, h - 1) * w + std::clamp(x + offsets[o][0], 0, w - 1);
                    if (maskBuffer[j] > nTag) {
                        maskBuffer[i] = nTag;
                        colorBuffer[i].r = 
                        colorBuffer[i].g = 
                        colorBuffer[i].b = 0;
                        ++n;
                        break;
                    }
                }
            }
        }
        a -= 256 >> nPasses;
    }
}


static void Outline(RGBA8* colorBuffer, int w, int h, int nPasses) {
    ManagedArray<uint8_t> maskBuffer(w * h);
    PrepareOutline(colorBuffer, maskBuffer.Data(), w * h);
    ComputeOutline(colorBuffer, maskBuffer.Data(), w, h, nPasses);
}


template <typename T>
static void BoxBlurH(T* dest, T* src, int w, int h, int r)
{
    for (int y = 0; y < h; ++y) {
        const int row = y * w;
        int a[3] = { 0, 0, 0 };
        int n = 0;

        const int xr = std::min(r, w - 1);
        for (int x = 0; x <= xr; ++x) {
            T& c = src[row + x];
            if (c.IsVisible()) {
                a[0] += (int)c.r;
                a[1] += (int)c.g;
                a[2] += (int)c.b;
                ++n;
            }
        }

        for (int x = 0; x < w; ++x) {
            T& srcColor = src[row + x];
            T& destColor = dest[row + x];

            if (n and srcColor.IsVisible()) {
                if constexpr (std::is_same_v<T, RGBA8>) {
                    destColor.a = srcColor.a;
                }
                destColor.r = (uint8_t)(a[0] / n);
                destColor.g = (uint8_t)(a[1] / n);
                destColor.b = (uint8_t)(a[2] / n);
            }
            else {
                destColor = srcColor;
            }

            int removeIdx = x - r;
            int addIdx = x + r + 1;

            if (removeIdx >= 0) {
                T& c = src[row + removeIdx];
                if (c.IsVisible()) {
                    a[0] -= (int)c.r;
                    a[1] -= (int)c.g;
                    a[2] -= (int)c.b;
                    --n;
                }
            }
            if (addIdx < w) {
                T& c = src[row + addIdx];
                if (c.IsVisible()) {
                    a[0] += (int)c.r;
                    a[1] += (int)c.g;
                    a[2] += (int)c.b;
                    ++n;
                }
            }
        }
    }
}


template <typename T>
static void BoxBlurV(T* dest, T* src, int w, int h, int r)
{
    for (int x = 0; x < w; ++x) {
        int a[3] = { 0, 0, 0 };
        int n = 0;

        int yr = std::min(r, h - 1);
        for (int y = 0; y <= yr; ++y) {
            T& c = src[y * w + x];
            if (c.IsVisible()) {
                a[0] += (int)c.r;
                a[1] += (int)c.g;
                a[2] += (int)c.b;
                ++n;
            }
        }

        for (int y = 0; y < h; ++y) {
            T& srcColor = src[y * w + x];
            T& destColor = dest[y * w + x];

            if (n and srcColor.IsVisible()) {
                if constexpr (std::is_same_v<T, RGBA8>) {
                    destColor.a = srcColor.a;
                }
                destColor.r = (uint8_t)(a[0] / n);
                destColor.g = (uint8_t)(a[1] / n);
                destColor.b = (uint8_t)(a[2] / n);
            }
            else {
                destColor = srcColor;
            }

            int removeIdx = y - r;
            int addIdx = y + r + 1;

            if (removeIdx >= 0) {
                T& c = src[removeIdx * w + x];
                if (c.IsVisible()) {
                    a[0] -= (int)c.r;
                    a[1] -= (int)c.g;
                    a[2] -= (int)c.b;
                    --n;
                }
            }
            if (addIdx < h) {
                T& c = src[addIdx * w + x];
                if (c.IsVisible()) {
                    a[0] += (int)c.r;
                    a[1] += (int)c.g;
                    a[2] += (int)c.b;
                    ++n;
                }
            }
        }
    }
}

using GaussKernel = SimpleArray<double, 31>;

static void ComputeKernel(GaussKernel& kernel, int r) {
    kernel.fill(0.0);
    double sigma = r > 0 ? r * 0.5 : 1.0;
    double s2 = 2.0 * sigma * sigma;
    double norm = 0.0;

    for (int k = -r; k <= r; ++k) {
        double v = r == 0 ? 1.0 : std::exp(-(double)(k * k) / s2);
        kernel[15 + k] = v;
        norm += v;
    }
    double inv = 1.0 / norm;
    for (int k = -r; k <= r; ++k) 
        kernel[15 + k] *= inv;
}


// Kernel-Caching: ein Eintrag pro Radius 0..15, Länge 31 (Mitte = Index 15)
static GaussKernel& GetKernel(int r)
{
    static SimpleArray<GaussKernel, 16> kernels;
    static bool haveKernel[16] = { false };

    if (not haveKernel[r]) {
        haveKernel[r] = true;
        ComputeKernel(kernels[r], r);
    }
    return kernels[r];
}

template <typename T>
static void GaussBlurH(T* dest, T* src, int w, int h, int r)
{
    GaussKernel& kernel = GetKernel(r);

    for (int y = 0; y < h; ++y) {
        int row = y * w;
        for (int x = 0; x < w; ++x) {
            T& srcColor = src[row + x];
            T& destColor = dest[row + x];

            double a[3] = { 0.0, 0.0, 0.0 };
            double wsum = 0.0;

            for (int k = -r; k <= r; ++k) {
                int sx = x + k;
                if (sx >= 0 and sx < w) {
                    T& c = src[row + sx];
                    if (c.IsVisible()) {
                        double wv = kernel[15 + k];
                        a[0] += wv * (int)c.r;
                        a[1] += wv * (int)c.g;
                        a[2] += wv * (int)c.b;
                        wsum += wv;
                    }
                }
            }

            if (wsum > 0.0 and srcColor.IsVisible()) {
                if constexpr (std::is_same_v<T, RGBA8>) destColor.a = srcColor.a;
                destColor.r = (uint8_t)(a[0] / wsum + 0.5);
                destColor.g = (uint8_t)(a[1] / wsum + 0.5);
                destColor.b = (uint8_t)(a[2] / wsum + 0.5);
            }
            else {
                destColor = srcColor;
            }
        }
    }
}

template <typename T>
static void GaussBlurV(T* dest, T* src, int w, int h, int r)
{
    GaussKernel& kernel = GetKernel(r);

    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            T& srcColor = src[y * w + x];
            T& destColor = dest[y * w + x];

            double a[3] = { 0.0, 0.0, 0.0 };
            double wsum = 0.0;

            for (int k = -r; k <= r; ++k) {
                int sy = y + k;
                if (sy >= 0 and sy < h) {
                    T& c = src[sy * w + x];
                    if (c.IsVisible()) {
                        double wv = kernel[15 + k];
                        a[0] += wv * (int)c.r;
                        a[1] += wv * (int)c.g;
                        a[2] += wv * (int)c.b;
                        wsum += wv;
                    }
                }
            }

            if (wsum > 0.0 and srcColor.IsVisible()) {
                if constexpr (std::is_same_v<T, RGBA8>) destColor.a = srcColor.a;
                destColor.r = (uint8_t)(a[0] / wsum + 0.5);
                destColor.g = (uint8_t)(a[1] / wsum + 0.5);
                destColor.b = (uint8_t)(a[2] / wsum + 0.5);
            }
            else {
                destColor = srcColor;
            }
        }
    }
}


void TextureBuffer::BoxBlur(uint16_t strength) {
    if (m_info.m_componentCount == 3) {
        ManagedArray<RGB8> blurBuffer(m_info.m_width * m_info.m_height);
        BoxBlurH<RGB8>(blurBuffer.Data(), reinterpret_cast<RGB8*>(m_data.Data()), m_info.m_width, m_info.m_height, int(strength));
        BoxBlurV<RGB8>(reinterpret_cast<RGB8*>(m_data.Data()), blurBuffer.Data(), m_info.m_width, m_info.m_height, int(strength));
    }
    else if (m_info.m_componentCount == 4) {
        ManagedArray<RGBA8> blurBuffer(m_info.m_width * m_info.m_height);
        BoxBlurH<RGBA8>(blurBuffer.Data(), reinterpret_cast<RGBA8*>(m_data.Data()), m_info.m_width, m_info.m_height, int(strength));
        BoxBlurV<RGBA8>(reinterpret_cast<RGBA8*>(m_data.Data()), blurBuffer.Data(), m_info.m_width, m_info.m_height, int(strength));
    }
}


void TextureBuffer::GaussBlur(uint16_t strength) {
    if (m_info.m_componentCount == 3) {
        ManagedArray<RGB8> blurBuffer(m_info.m_width * m_info.m_height);
        GaussBlurH<RGB8>(blurBuffer.Data(), reinterpret_cast<RGB8*>(m_data.Data()), m_info.m_width, m_info.m_height, int(strength));
        GaussBlurV<RGB8>(reinterpret_cast<RGB8*>(m_data.Data()), blurBuffer.Data(), m_info.m_width, m_info.m_height, int(strength));
    }
    else if (m_info.m_componentCount == 4) {
        ManagedArray<RGBA8> blurBuffer(m_info.m_width * m_info.m_height);
        GaussBlurH<RGBA8>(blurBuffer.Data(), reinterpret_cast<RGBA8*>(m_data.Data()), m_info.m_width, m_info.m_height, int(strength));
        GaussBlurV<RGBA8>(reinterpret_cast<RGBA8*>(m_data.Data()), blurBuffer.Data(), m_info.m_width, m_info.m_height, int(strength));
    }
}


void TextureBuffer::Posterize(uint16_t gradients) {
    if (not m_isPosterized) {
        m_isPosterized = true;
        if (m_info.m_componentCount == 3)
            ::Posterize<RGB8>(reinterpret_cast<RGB8*>(m_data.Data()), m_info.m_width * m_info.m_height, gradients);
        else if (m_info.m_componentCount == 4)
            ::Posterize<RGBA8>(reinterpret_cast<RGBA8*>(m_data.Data()), m_info.m_width * m_info.m_height, gradients);
        }
}


void TextureBuffer::Cartoonize(uint16_t blurStrength, uint16_t gradients, uint16_t outlinePasses) {
    if (not m_isCartoonized) {
        m_isCartoonized = true;
        Posterize(gradients);
        GaussBlur(blurStrength);
        if (m_info.m_componentCount == 4) 
            ::Outline(reinterpret_cast<RGBA8*>(m_data.Data()), m_info.m_width, m_info.m_height, int(outlinePasses));
    }
}

// =================================================================================================
