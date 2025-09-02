
#include "texturehandler.h"

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


#if 0
TextureList TextureHandler::Create(String textureFolder, List<String>& textureNames, TextureGetter getTexture) {
    return CreateTextures(textureFolder, textureNames, getTexture);
}
#endif

TextureList TextureHandler::CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, bool premultiply) {
    TextureList textures;
    for (auto& n : textureNames) {
        Texture* t = getTexture();
        if (not t)
            break;
        textures.Append(t);
        List<String> fileNames; // must be local here so it gets reset every loop iteration
        fileNames.Append(textureFolder + n);
        if (not t->CreateFromFile(fileNames, premultiply))
            break;
    }
    return textures;
}


TextureList TextureHandler::CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType, bool premultiply) {
    return CreateTextures(textureFolder, textureNames, [&]() { return (textureType == GL_TEXTURE_CUBE_MAP) ? GetCubemap() : GetStandardTexture(); }, premultiply);
}

// =================================================================================================
