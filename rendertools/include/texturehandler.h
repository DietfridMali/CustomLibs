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

    template <typename T>
    T* GetTexture(void) {
        T* t = new T();
        if (t)
            m_textures.Append(t);
        return t;
    }

    using TextureGetter = std::function<Texture*()>;

    inline Texture* GetStandardTexture(void) {
        return GetTexture<Texture>();
    }

    inline Texture* GetLinearTexture(void) {
        return GetTexture<LinearTexture>();
    }

    inline TiledTexture* GetTiledTexture(void) {
        return GetTexture<TiledTexture>();
    } 

    inline Cubemap* GetCubemap(void) {
        return GetTexture<Cubemap>();
    }

    bool Remove(Texture* texture);

#if 0
    TextureList Create(String textureFolder, List<String>& textureNames, GLenum textureType);
#endif

    TextureList CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, bool premultiply = false);

    inline TextureList CreateStandardTextures(String textureFolder, List<String>& textureNames, bool premultiply = false) {
        return CreateTextures(textureFolder, textureNames, [&]() { return GetStandardTexture(); }, premultiply);
    }

    TextureList CreateTiledTextures(String textureFolder, List<String>& textureNames, bool premultiply = false) {
        return CreateTextures(textureFolder, textureNames, [&]() { return GetTiledTexture(); }, premultiply);
    }

    TextureList CreateCubemaps(String textureFolder, List<String>& textureNames, bool premultiply = false) {
        return CreateTextures(textureFolder, textureNames, [&]() { return GetCubemap(); }, premultiply);
    }

    TextureList CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType, bool premultiply = false);

};

#define textureHandler TextureHandler::Instance()

// =================================================================================================
