
#include <utility>
#include <stdio.h>
#include "texture.h"
#include "SDL_image.h"
#include "opengl_states.h"

// =================================================================================================

SharedTextureHandle Texture::nullHandle = SharedTextureHandle(0);

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
        struct RGBA8 {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };
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
    m_info.m_internalFormat = (componentCount == 1) ? GL_R8 : (componentCount == 3) ? GL_RGB : GL_RGBA;
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
    m_info = other.m_info;
    m_data = other.m_data;
    return *this;
}

TextureBuffer& TextureBuffer::Move(TextureBuffer& other)
noexcept
{
    m_info = other.m_info;
    m_data = std::move(other.m_data);
    other.Reset();
    return *this;
}

// =================================================================================================

bool Texture::Create(void) {
    Destroy();
#if USE_SHARED_HANDLES
    m_handle = SharedTextureHandle();
    return m_handle.Claim() != 0;
#else
    glGenTextures(1, &m_handle);
    return m_handle != 0;
#endif
}


void Texture::Destroy(void)
{
#if USE_SHARED_HANDLES
    m_handle.Release();
#else
    glDeleteTextures(1, &m_handle);
    m_handle = 0;
#endif
    TextureBuffer* texBuf = nullptr;
    for (const auto& p : m_buffers) {
        if (p != texBuf) {
            texBuf = p;
            delete p;
        }
    }
    m_buffers.Clear();
    m_hasBuffer = false; // BUGFIX: Status zurücksetzen
}


Texture& Texture::Copy(const Texture& other) {
    if (this != &other) {
        Destroy();
        m_handle = other.m_handle;
        m_name = other.m_name;
        m_buffers = other.m_buffers;
        m_filenames = other.m_filenames;
        m_type = other.m_type;
        m_wrapMode = other.m_wrapMode;
        m_useMipMaps = other.m_useMipMaps;
        m_hasBuffer = other.m_hasBuffer; 
        m_hasParams = other.m_hasParams;
        m_isValid = other.m_isValid;     
    }
    return *this;
}


Texture& Texture::Move(Texture& other)
noexcept
{
    if (this != &other) {
        Destroy();
        m_handle = std::move(other.m_handle);
#if !USE_SHARED_HANDLES
        other.m_handle = 0;
#endif
        m_name = std::move(other.m_name);
        m_buffers = std::move(other.m_buffers);
        m_filenames = std::move(other.m_filenames);
        m_type = other.m_type;
        m_wrapMode = other.m_wrapMode;
        m_useMipMaps = other.m_useMipMaps;
        m_hasBuffer = other.m_hasBuffer; 
        m_hasParams = other.m_hasParams;
        m_isValid = other.m_isValid;     
    }
    return *this;
}

bool Texture::IsAvailable(void)
{
    return
#if USE_SHARED_HANDLES
        m_handle.IsAvailable()
#else
        (m_handle != 0)
#endif
        and (HasBuffer() or not m_buffers.IsEmpty());
}


bool Texture::Bind(int tmuIndex)
{
    if (not IsAvailable())
        return false;
    m_tmuIndex = tmuIndex;
#if USE_SHARED_HANDLES
    openGLStates.BindTexture(m_type, m_handle.Data(), tmuIndex);
#else
    openGLStates.BindTexture(m_type, m_handle, tmuIndex);
#endif
    return true;
}


void Texture::Release(int tmuIndex)
{
    if (IsAvailable()) {
        if (m_tmuIndex >= 0) {
            openGLStates.BindTexture(m_type, 0, m_tmuIndex);
            m_tmuIndex = -1;
        }
        //openGLStates.ActiveTexture(GL_TEXTURE0);
    }
}


void Texture::SetParams(bool enforce)
{
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        if (m_useMipMaps) {
            glTexParameteri(m_type, GL_GENERATE_MIPMAP, GL_TRUE);
            glTexParameteri(m_type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(m_type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        else {
            glTexParameteri(m_type, GL_GENERATE_MIPMAP, GL_FALSE);
            glTexParameteri(m_type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(m_type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glTexParameteri(m_type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(m_type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}


void Texture::SetWrapping(int wrapMode)
noexcept
{
    if (wrapMode >= 0)
        m_wrapMode = wrapMode;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_wrapMode);
}


bool Texture::Enable(int tmuIndex)
{
    if (not Bind(tmuIndex))
        return false;
    //SetParams();
    //SetWrapping();
    return true;
}


void Texture::Disable(int tmuIndex)
{
    Release(tmuIndex);
}


void Texture::Deploy(int bufferIndex)
{
    if (Bind()) {
        SetParams();
        TextureBuffer* texBuf = m_buffers[bufferIndex];
        glTexImage2D(m_type, 0, texBuf->m_info.m_internalFormat, texBuf->m_info.m_width, texBuf->m_info.m_height, 0, texBuf->m_info.m_format, GL_UNSIGNED_BYTE, reinterpret_cast<const void*>(texBuf->m_data.Data()));
        Release();
    }
}

// -------------------------------------------------------------------------------------------------
// Load loads textures from file. The texture filenames are given in filenames
// An empty filename ("") means that the previously loaded texture should be used here as well
// This makes sense e.g. for cubemaps if several of its faces share the same texture, like e.g. spherical smileys,
// which have a face on one side and flat color on the remaining five sides of the cubemap used to texture them.
// So a smiley cubemap texture list could be specified here like this: ("skin.png", "", "", "", "", "face.png")
// This would cause the skin texture to be reused for in the place of the texture data buffers at positions 2, 3, 4
// and 5. You could also do something like ("skin.png", "", "back.png", "", "face.png") or just ("color.png", "", "", "", "", "")
// for a uniformly textured sphere. The latter case will however be also taken into regard by the cubemap class.
// It allows to pass a single texture which it will use for all faces of the cubemap

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <iostream>

#ifdef _DEBUG

static void CheckFileOpen(const std::string& path) {
    errno = 0;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::cerr << "fopen failed for \"" << path << "\"\n";
        std::cerr << "errno=" << errno << " (" << std::strerror(errno) << ")\n";
        std::cerr << "bytes:";
        for (auto c : path) {
            std::cerr << " " << std::hex << (int)c;
        }
        std::cerr << std::dec << "\n";
    }
    else {
        std::cout << "fopen OK for \"" << path << "\"\n";
        std::fclose(f);
    }
}

#endif

bool Texture::Load(List<String>& fileNames, bool premultiply, bool flipVertically) {
    // load texture from file
    m_filenames = fileNames;
    m_name = fileNames.First();
    TextureBuffer* texBuf = nullptr;
#ifdef _DEBUG
    String bufferName = "";
#endif
    for (auto& fileName : fileNames) {
        if (fileName.IsEmpty()) { // must never be true for first filename
            if (not texBuf) // must always be true here -> fileNames[0] must always contain a valid filename of an existing texture file
                throw std::runtime_error("Texture::Load: missing texture names");
            else {
                m_buffers.Append(texBuf);
#ifdef _DEBUG
                texBuf->m_name = bufferName;
#endif
            }
        }
        else {
#if 0 // def _DEBUG
            bufferName = fileName;
            CheckFileOpen(fileName);
#endif
            SDL_Surface* image = IMG_Load(fileName.Data());
            if (not image) {
                const char* imgError = IMG_GetError();
                fprintf(stderr, "Couldn't load '%s (%s)'\n", (char*)(fileName), imgError);
                return false;
            }
            texBuf = new TextureBuffer();
            if (texBuf) {
                texBuf->Create(image, premultiply, flipVertically);
                m_buffers.Append(texBuf);
#ifdef _DEBUG
                texBuf->m_name = bufferName;
#endif
            }
        }
    }
    return true;
}


bool Texture::CreateFromFile(List<String>& fileNames, bool premultiply, bool flipVertically) {
    if (not Create())
        return false;
    if (fileNames.IsEmpty())
        return true;
    if (not Load(fileNames, flipVertically))
        return false;
    Deploy();
    return true;
}


bool Texture::CreateFromSurface(SDL_Surface* surface, bool premultiply, bool flipVertically) {
    if (not Create())
        return false;
    m_buffers.Append(new TextureBuffer(surface, premultiply, flipVertically));
    return true;
}


tRenderOffsets Texture::ComputeOffsets(int w, int h, int viewportWidth, int viewportHeight, int renderAreaWidth, int renderAreaHeight)
noexcept
{
    if (renderAreaWidth == 0)
        renderAreaWidth = viewportWidth;
    if (renderAreaHeight == 0)
        renderAreaHeight = viewportHeight;
    float xScale = float(renderAreaWidth) / float(viewportWidth);
    float yScale = float(renderAreaHeight) / float(viewportHeight);
    float wRatio = float(renderAreaWidth) / float(w);
    float hRatio = float(renderAreaHeight) / float(h);
    tRenderOffsets offsets = { 0.5f * xScale, 0.5f * yScale };
    if (wRatio > hRatio)
        offsets.x -= (float(renderAreaWidth) - float(w) * hRatio) / float(2 * viewportWidth);
    else if (wRatio < hRatio)
        offsets.y -= (float(renderAreaHeight) - float(h) * wRatio) / float(2 * viewportHeight);
    return offsets;
}

// =================================================================================================

void TiledTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        Texture::SetParams();
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenerateMipmap(GL_TEXTURE_2D);

        // (optional) Anisotropie
        GLfloat aniso = 0.f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &aniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    }
}

// =================================================================================================

void FBOTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if 1
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
#endif
    }
}

// =================================================================================================
