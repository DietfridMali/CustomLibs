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

    bool DeleteTextures(const String& key, Texture** texture);

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
    T* GetTexture(String& name) {
        T* t = new T();
        if (t) {
            t->Register(name);
        }
        return t;
    }

    Texture* FindTexture(String& name);


    using TextureGetter = std::function<Texture*(String& name)>;

    inline Texture* GetStandardTexture(String& name) {
        return GetTexture<Texture>(name);
    }

    template <typename DATA_T>
    inline Texture* GetLinearTexture(String& name) {
        return GetTexture<LinearTexture<DATA_T>>(name);
    }

    inline TiledTexture* GetTiledTexture(String& name) {
        return GetTexture<TiledTexture>(name);
    } 

    inline Cubemap* GetCubemap(String& name) {
        return GetTexture<Cubemap>(name);
    }

    TextureList CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, const TextureCreationParams& params);

    inline TextureList CreateStandardTextures(String textureFolder, List<String>& textureNames, const TextureCreationParams& params) {
        return CreateTextures(textureFolder, textureNames, [&](String& name) { return GetStandardTexture(name); }, params);
    }

    TextureList CreateTiledTextures(String textureFolder, List<String>& textureNames, const TextureCreationParams& params) {
        return CreateTextures(textureFolder, textureNames, [&](String& name) { return GetTiledTexture(name); }, params);
    }

    TextureList CreateCubemaps(String textureFolder, List<String>& textureNames, const TextureCreationParams& params) {
        return CreateTextures(textureFolder, textureNames, [&](String& name) { return GetCubemap(name); }, params);
    }

    TextureList CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType, const TextureCreationParams& params);

};

#define textureHandler TextureHandler::Instance()

// =================================================================================================
