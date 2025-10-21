
#include "texturehandler.h"
#include "list.hpp"

// =================================================================================================
// Very simple class for texture tracking
// Main purpose is to keep track of all texture objects in the game and return them to OpenGL in
// a well defined and controlled way at program termination without having to bother about releasing
// textures at a dozen places in the game


bool TextureHandler::DeleteTextures(const size_t& key, Texture** texture) {
    if (texture and ((*texture)->m_id != 0)) {
        delete* texture;
        *texture = nullptr;
    }
    return true;
}


void TextureHandler::Destroy(void) noexcept {
    Texture::UpdateLUT(false);
    Texture::textureLUT.Walk(&TextureHandler::DeleteTextures, this);
}


TextureList TextureHandler::CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, const TextureCreationParams& params) {
    GetTextureFolder(textureFolder);
    TextureList textures;
    for (auto& n : textureNames) {
        List<String> fileNames; // must be local here so it gets reset every loop iteration
        fileNames.Append(textureFolder + n);
        Texture* t = getTexture();
        if (not (t and t->CreateFromFile(fileNames, params))) {
#ifdef _DEBUG
            fprintf(stderr, "Couldn't load texture '%s'\n", (char*)n);
#endif
            for (auto& h : textures)
                delete h;
            textures.Clear();
            break;
        }
        textures.Append(t);
    }
    return textures;
}


TextureList TextureHandler::CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType, const TextureCreationParams& params) {
    return CreateTextures(textureFolder, textureNames, [&]() { return (textureType == GL_TEXTURE_CUBE_MAP) ? GetCubemap() : GetStandardTexture(); }, params);
}

// =================================================================================================
