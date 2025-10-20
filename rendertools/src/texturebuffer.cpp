
#include <utility>
#include <stdio.h>
#include <stdexcept>
#include "texturebuffer.h"
#include "SDL_image.h"
#include "opengl_states.h"

// =================================================================================================

struct RGBA8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
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
        fprintf(stderr, "%s (%d): memory allocation for texture clone failed\n", __FILE__, __LINE__);
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


static void Posterize(RGBA8* colorBuffer, int bufSize, uint16_t gradients)
{
    for (; bufSize; --bufSize, ++colorBuffer) {
        if (colorBuffer->a) {
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

    for (int nPass = 0; nPass < nPasses; --nPass, --nTag) {
        for (int y = 0; y < h; ++y) {
            int i = y * w;
            for (int x = 0; x < w; ++x, ++i) {
                if (maskBuffer[i])
                    continue;
                for (int o = 0; o < 8; ++o) {
                    int j = std::clamp(y + offsets[o][1], 0, h) * w + std::clamp(x + offsets[o][0], 0, w);
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


static void BoxBlurH(RGBA8* dest, RGBA8* src, int w, int h, int r)
{
    int i = 0;
    for (int y = 0; y < h; ++y) {
        int a[3] = { 0, 0, 0 };
        int n = 0;

        for (int x = -r; x < w; ++x) {
            int j = x - r - 1;
            if (j >= 0) {
                RGBA8& color = src[i + j];
                if (color.a) {
                    a[0] -= (int)color.r;
                    a[1] -= (int)color.g;
                    a[2] -= (int)color.b;
                    --n;
                }
            }

            j = x + r;
            if (j < w) {
                RGBA8& color = src[i + j];
                if (color.a) {
                    a[0] += (int)color.r;
                    a[1] += (int)color.g;
                    a[2] += (int)color.b;
                    ++n;
                }
            }

            if (x >= 0) {
                RGBA8& srcColor = src[i + x];
                RGBA8& destColor = dest[i + x];
                if (n and srcColor.a) {
                    destColor.a = srcColor.a;
                    destColor.r = uint8_t(a[0] / n);
                    destColor.g = uint8_t(a[1] / n);
                    destColor.b = uint8_t(a[2] / n);
                }
                else
                    destColor = srcColor;
            }
        }
        ++i;
    }
}


static void BoxBlurV(RGBA8* dest, RGBA8* src, int w, int h, int r)
{
    int o[2] = { -(r + 1), r };

    for (int x = 0; x < w; ++x) {
        int a[3] = { 0, 0, 0 };
        int n = 0;

        int i = -r + x;
        for (int y = -r; y < h; ++y) {
            int j = y - r - 1;
            if (j >= 0) {
                RGBA8& color = src[i + o[0]];
                if (color.a) {
                    a[0] -= (int)color.r;
                    a[1] -= (int)color.g;
                    a[2] -= (int)color.b;
                    --n;
                }
            }

            j = y + r;
            if (j < h) {
                RGBA8& color = src[i + o[1]];
                if (color.a) {
                    a[0] += (int)color.r;
                    a[1] += (int)color.g;
                    a[2] += (int)color.b;
                    ++n;
                }
            }

            if (y >= 0) {
                RGBA8& srcColor = dest[y * w + x];
                RGBA8& destColor = dest[y * w + x];
                if (n and srcColor.a) {
                    destColor.a = srcColor.a;
                    destColor.r = uint8_t(a[0] / n);
                    destColor.g = uint8_t(a[1] / n);
                    destColor.b = uint8_t(a[2] / n);
                }
                else
                    destColor = srcColor;
            }
            ++i;
        }
    }
}

void TextureBuffer::BoxBlur(int r) {
    ManagedArray<RGBA8> blurBuffer(m_info.m_width * m_info.m_height * 4);
    BoxBlurH(blurBuffer.Data(), reinterpret_cast<RGBA8*>(m_data.Data()), m_info.m_width, m_info.m_height, r);
    BoxBlurV(reinterpret_cast<RGBA8*>(m_data.Data()), blurBuffer.Data(), m_info.m_width, m_info.m_height, r);
}


void TextureBuffer::Posterize(uint32_t gradients) {
    if (not m_isPosterized) {
        m_isPosterized = true;
        ::Posterize(reinterpret_cast<RGBA8*>(m_data.Data()), m_info.m_width * m_info.m_height, gradients);
    }
}


void TextureBuffer::Cartoonize(void) {
    BoxBlur(4);
    Posterize();
    ::Outline(reinterpret_cast<RGBA8*>(m_data.Data()), m_info.m_width, m_info.m_height, 4);
}

// =================================================================================================
