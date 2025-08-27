#pragma once

#include "glew.h"
#include "texture.h"
#include "cubemap.h"
#include "list.hpp"
#include "sharedpointer.hpp"
#include "singletonbase.hpp"

// =================================================================================================
// Very simply class for texture tracking
// Main purpose is to keep track of all texture objects in the game and return them to OpenGL in
// a well defined and controlled way at program termination without having to bother about releasing
// textures at a dozen places in the game

class TextureHandler
    : public BaseSingleton<TextureHandler>
{
public:
    TextureList m_textures;

    typedef Texture* (*tGetter) (void);

    TextureHandler() = default;

    ~TextureHandler() noexcept { Destroy(); }

    void Destroy(void) noexcept;

#if 0
    Texture* GetTexture(void);
#else
    template <typename T>
    T* TextureHandler::GetTexture(void) {
        T* t = new T();
        if (t)
            m_textures.Append(t);
        return t;
    }
#endif

    using TextureGetter = std::function<Texture*()>;

    Using GetStandardTexture = GetTexture<Texture>;

    Using GetTiledTexture = GetTexture<TiledTexture>;

    Using GetCubemap = GetTexture<Cubemap>;

    Cubemap* GetCubemap(void);

    bool Remove(Texture* texture);

#if 0
    TextureList Create(String textureFolder, List<String>& textureNames, GLenum textureType);
#endif

    TextureList CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture);

    inline TextureList CreateStandardTextures(String textureFolder, List<String>& textureNames) {
        return CreateTextures(textureFolder, textureNames, GetStandardTexture);
    }

    TextureList CreateTiledTextures(String textureFolder, List<String>& textureNames) {
        return CreateTextures(textureFolder, textureNames, GetTiledTexture);
    }

    TextureList CreateCubemaps(String textureFolder, List<String>& textureNames, GetTexture<Cubemap>) {
        return CreateTextures(textureFolder, textureNames, GetCubemap);
    }

    TextureList CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType);

};

#define textureHandler TextureHandler::Instance()

// =================================================================================================
