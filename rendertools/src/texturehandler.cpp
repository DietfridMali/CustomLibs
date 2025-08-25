
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


Texture* TextureHandler::GetTexture(void) {
    Texture* t = new Texture();
    if (not t)
        return nullptr;
    m_textures.Append(t);
    return t;
}


bool TextureHandler::Remove(Texture* texture) {
    if (not texture)
        return false;
    m_textures.Remove(texture);
    texture->Destroy();
    delete texture; // BUGFIX: Speicherleck beseitigt (Objekt selbst freigeben)
    return true;
}


Cubemap* TextureHandler::GetCubemap(void) {
    Cubemap* t = new Cubemap();
    m_textures.Append(t);
    return t;
}


TextureList TextureHandler::Create(String textureFolder, List<String>& textureNames, GLenum textureType) {
    return (textureType == GL_TEXTURE_CUBE_MAP) ? CreateCubemaps(textureFolder, textureNames) : CreateTextures(textureFolder, textureNames);
}


TextureList TextureHandler::CreateTextures(String textureFolder, List<String>& textureNames) {
    TextureList textures;
    for (auto& n : textureNames) {
        Texture* t = GetTexture();
        if (not t)
            break;
        textures.Append(t);
        List<String> fileNames; // must be local here so it gets reset every loop iteration
        fileNames.Append(textureFolder + n);
        if (not t->CreateFromFile(fileNames))
            break;
    }
    return textures;
}


TextureList TextureHandler::CreateCubemaps(String textureFolder, List<String>& textureNames) {
    TextureList textures;
    List<String> fileNames;
    for (auto& n : textureNames) {
        Cubemap* t = GetCubemap();
        if (not t)
            break;
        textures.Append(t);
        fileNames.Append(textureFolder + n);
        if (not t->CreateFromFile(fileNames))
            break;
    }
    return textures;
}


TextureList TextureHandler::CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType) {
    return Create(textureFolder, textureNames, textureType);
}

// =================================================================================================
