
#include "texturehandler.h"
#include "list.hpp"

// =================================================================================================
// Very simple class for texture tracking
// Main purpose is to keep track of all texture objects in the game and return them to OpenGL in
// a well defined and controlled way at program termination without having to bother about releasing
// textures at a dozen places in the game


bool TextureHandler::DeleteTextures(const size_t& key, Texture** texture) {
    assert(texture->m_id != 0);
    delete *texture;
    *texture = nullptr;
    return true;
}


void TextureHandler::Destroy(void) noexcept {
    Texture::UpdateLUT(false);
    Texture::textureLUT.Walk(&TextureHandler::DeleteTextures, this);
}


TextureList TextureHandler::CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, bool premultiply) {
    GetTextureFolder(textureFolder);
    TextureList textures;
    for (auto& n : textureNames) {
        List<String> fileNames; // must be local here so it gets reset every loop iteration
        fileNames.Append(textureFolder + n);
        Texture* t = getTexture();
        if (not (t and t->CreateFromFile(fileNames, premultiply))) {
            for (auto& h : textures)
                delete h;
            textures.Clear();
            break;
        }
        textures.Append(t);
    }
    return textures;
}


TextureList TextureHandler::CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType, bool premultiply) {
    return CreateTextures(textureFolder, textureNames, [&]() { return (textureType == GL_TEXTURE_CUBE_MAP) ? GetCubemap() : GetStandardTexture(); }, premultiply);
}

// =================================================================================================
