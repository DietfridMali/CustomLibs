
#include <utility>
#include <stdio.h>
#include <stdexcept>
#include "texture.h"
#include "SDL_image.h"
#include "opengl_states.h"
#include "base_renderer.h"

#define USE_TEXTURE_LUT 1

// =================================================================================================

SharedTextureHandle Texture::nullHandle = SharedTextureHandle(0);

int Texture::CompareTextures(void* context, const String& key1, const String& key2) {
    int i = String::Compare(nullptr, key1, key2);
    return (i < 0) ? -1 : (i > 0) ? 1 : 0;
}


Texture::Texture(GLuint handle, int type, int wrapMode)
    : m_handle(handle)
    , m_type(type)
    , m_tmuIndex(-1)
    , m_wrapMode(wrapMode)
    , m_name("")
{
#if USE_TEXTURE_LUT
    SetupLUT();
#endif
}


Texture::~Texture()
noexcept
{
    if (m_isValid) {
#if USE_TEXTURE_LUT
        if (UpdateLUT()) {
            textureLUT.Remove(m_name);
            m_name = "";
        }
#endif
        Destroy();
    }
}


bool Texture::Create(void) {
    Destroy();
#if USE_SHARED_HANDLES
    m_handle = SharedTextureHandle();
    m_isValid = m_handle.Claim() != 0;
#else
    glGenTextures(1, &m_handle);
    m_isValid = m_handle != 0;
#endif
    return m_isValid;
}


void Texture::Destroy(void)
{
    if (m_isValid) {
        m_isValid = false;
#if USE_SHARED_HANDLES
        m_handle.Release();
#else
        glDeleteTextures(1, &m_handle);
        m_handle = 0;
#endif
        for (const auto& p : m_buffers) {
            if (p->m_refCount)
                --(p->m_refCount);
            else
                delete p;
        }
        m_buffers.Clear();
        m_hasBuffer = false; // BUGFIX: Status zurücksetzen
    }
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
        textureLUT.Remove(m_name);
        textureLUT.Insert(m_name, this, true); // overwrite the data entry for key m_id with this texture
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
            glTexParameteri(m_type, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(m_type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glGenerateMipmap(m_type);
        }
        else {
            glTexParameteri(m_type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(m_type, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(m_type, GL_TEXTURE_MAX_LEVEL, 0);
        }
        glTexParameteri(m_type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(m_type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}


void Texture::SetWrapping(int wrapMode)
noexcept
{
    if (Bind()) {
        if (wrapMode >= 0)
            m_wrapMode = wrapMode;
        glTexParameteri(m_type, GL_TEXTURE_WRAP_S, m_wrapMode);
        glTexParameteri(m_type, GL_TEXTURE_WRAP_T, m_wrapMode);
        Release();
    }
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


void Texture::Cartoonize(uint16_t blurStrength, uint16_t gradients, uint16_t outlinePasses) {
    for (auto& b : m_buffers)
        b->Cartoonize(blurStrength, gradients, outlinePasses);
}


void Texture::Deploy(int bufferIndex)
{
    if (Bind()) {
        SetParams();
        TextureBuffer* texBuf = m_buffers[bufferIndex];
        uint32_t* data = reinterpret_cast<uint32_t*>(texBuf->m_data.Data());
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

// Load does a "trick" for loading cubemaps
// If a cubemap uses the same texture for subsequent sides, it can specify "" as filename for these sides
// Load will load the texture, and for each subsequent side with texture name "", that texture's data buffer
// will be used. This is saving memory for smileys, where 5 sides bear the same texture, while only the front
// face bears the face.
bool Texture::Load(String& folder, List<String>& fileNames, const TextureCreationParams& params) {
    // load texture from file
    m_filenames = fileNames;
    m_name = fileNames.First();
    TextureBuffer* texBuf = nullptr;
#ifdef _DEBUG
    String bufferName = "";
#endif
    for (auto& fileName : fileNames) {
        if (fileName.IsEmpty()) { // This means that the last previously loaded texture should be used here as well; must never be true for first filename
            if (not texBuf) // must always be true here -> fileNames[0] must always contain a valid filename of an existing texture file
                throw std::runtime_error("Texture::Load: missing texture names");
            else {
                ++(texBuf->m_refCount);
                m_buffers.Append(texBuf);
#ifdef _DEBUG
                texBuf->m_name = bufferName;
#endif
            }
        }
        else {
            String fullName = folder + fileName;
#if 0 // def _DEBUG
            bufferName = fileName;
            CheckFileOpen(fullName);
#endif
            SDL_Surface* image = IMG_Load(fullName.Data());
            if (not image) {
                const char* imgError = IMG_GetError();
                fprintf(stderr, "Couldn't load '%s (%s)'\n", (char*)(fullName), imgError);
                return false;
            }
            texBuf = new TextureBuffer();
            if (texBuf) {
                texBuf->Create(image, params.premultiply, params.flipVertically);
                m_buffers.Append(texBuf);
#ifdef _DEBUG
                texBuf->m_name = bufferName;
#endif
            }
        }
    }
    return true;
}


bool Texture::CreateFromFile(String folder, List<String>& fileNames, const TextureCreationParams& params) {
    if (not Create())
        return false;
    if (fileNames.IsEmpty())
        return true;
    if (not Load(folder, fileNames, params))
        return false;
    if (params.cartoonize)
        Cartoonize(params.blur, params.gradients, params.outline);
    Deploy();
    return true;
}


bool Texture::CreateFromSurface(SDL_Surface* surface, const TextureCreationParams& params) {
    if (not Create())
        return false;
    m_buffers.Append(new TextureBuffer(surface, params.premultiply, params.flipVertically));
    return true;
}


RenderOffsets Texture::ComputeOffsets(int w, int h, int viewportWidth, int viewportHeight, int renderAreaWidth, int renderAreaHeight)
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
    RenderOffsets offsets = { 0.5f * xScale, 0.5f * yScale };
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
#if 1
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
#else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE); 
#if 1
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
#endif
    }
}

// =================================================================================================

void ShadowTexture::SetParams(bool enforce) {
    if (enforce or not m_hasParams) {
        m_hasParams = true;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS); // or GL_LEQUAL
#if 1
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
#endif
    }
}
// =================================================================================================
