
#include "texturehandler.h"
#include "list.hpp"

// =================================================================================================
// Very simple class for texture tracking
// Main purpose is to keep track of all texture objects in the game and return them to OpenGL in
// a well defined and controlled way at program termination without having to bother about releasing
// textures at a dozen places in the game

void TextureHandler::Destroy(void) noexcept {
    for (auto& t : m_textures)
        delete t;
    m_textures.Clear(); // verhindert Double-Delete bei wiederholtem Destroy
}


bool TextureHandler::Remove(Texture* texture) {
    if (not texture)
        return false;
    m_textures.Remove(texture);
    texture->Destroy();
    delete texture; // BUGFIX: Speicherleck beseitigt (Objekt selbst freigeben)
    return true;
}


TextureList TextureHandler::CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, bool premultiply) {
    if (textureFolder == "")
        textureFolder = m_textureFolder;
    TextureList textures;
    for (auto& n : textureNames) {
        Texture* t = getTexture();
        List<String> fileNames; // must be local here so it gets reset every loop iteration
        fileNames.Append(textureFolder + n);
        if (not ((t = getTexture()) and t->CreateFromFile(fileNames, premultiply))) {
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
