#pragma once

#include "glew.h"
#include "texture.h"
#include "cubemap.h"
#include "list.hpp"
#include "sharedpointer.hpp"
#include "basesingleton.hpp"

// =================================================================================================
// Very simply class for texture tracking
// Main purpose is to keep track of all texture objects in the game and return them to OpenGL in
// a well defined and controlled way at program termination without having to bother about releasing
// textures at a dozen places in the game

class TextureHandler
    : public BaseSingleton<TextureHandler>
{
public:
    String      m_textureFolder{ "" };
    uint32_t    m_textureID{ 0 };

    typedef Texture* (*tGetter) (void);

    TextureHandler() {
        Texture::textureLUT.SetComparator(&Texture::CompareTextures);
    }

    ~TextureHandler() noexcept { Destroy(); }

    bool DeleteTextures(const size_t& key, Texture** texture);

    void Destroy(void) noexcept;

    void SetTextureFolder(String textureFolder) noexcept {
        m_textureFolder = textureFolder;
    }

    String& GetTextureFolder(String& textureFolder) noexcept {
        if (textureFolder.Length() == 0) 
            textureFolder = m_textureFolder;
        return textureFolder;
    }

    template <typename T>
    T* GetTexture(void) {
        T* t = new T();
        if (t)
            t->Register();
        return t;
    }

    using TextureGetter = std::function<Texture*()>;

    inline Texture* GetStandardTexture(void) {
        return GetTexture<Texture>();
    }

    template <typename DATA_T>
    inline Texture* GetLinearTexture(void) {
        return GetTexture<LinearTexture<DATA_T>>();
    }

    inline TiledTexture* GetTiledTexture(void) {
        return GetTexture<TiledTexture>();
    } 

    inline Cubemap* GetCubemap(void) {
        return GetTexture<Cubemap>();
    }

    TextureList CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, const TextureCreationParams& params);

    inline TextureList CreateStandardTextures(String textureFolder, List<String>& textureNames, const TextureCreationParams& params) {
        return CreateTextures(textureFolder, textureNames, [&]() { return GetStandardTexture(); }, params);
    }

    TextureList CreateTiledTextures(String textureFolder, List<String>& textureNames, const TextureCreationParams& params) {
        return CreateTextures(textureFolder, textureNames, [&]() { return GetTiledTexture(); }, params);
    }

    TextureList CreateCubemaps(String textureFolder, List<String>& textureNames, const TextureCreationParams& params) {
        return CreateTextures(textureFolder, textureNames, [&]() { return GetCubemap(); }, params);
    }

    TextureList CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType, const TextureCreationParams& params);

};

#define textureHandler TextureHandler::Instance()

// =================================================================================================
