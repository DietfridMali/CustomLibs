
#include "texturehandler.h"
#include "list.hpp"

// =================================================================================================
// Very simple class for texture tracking
// Main purpose is to keep track of all texture objects in the game and return them to OpenGL in
// a well defined and controlled way at program termination without having to bother about releasing
// textures at a dozen places in the game


bool TextureHandler::DeleteTextures(const TextureID& key, Texture** texture) {
    if (texture and *texture and ((*texture)->m_id.ID != 0)) {
        delete* texture;
        *texture = nullptr;
    }
    return true;
}


void TextureHandler::Destroy(void) noexcept {
    Texture::UpdateLUT(false);
    Texture::textureLUT.Walk(&TextureHandler::DeleteTextures, this);
}


Texture* TextureHandler::FindTexture(String name) {
    TextureID id{ -1, name };
    Texture** t = Texture::textureLUT.Find(id);
    return t ? *t : nullptr;
}


TextureList TextureHandler::CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, const TextureCreationParams& params) {
    GetTextureFolder(textureFolder);
    TextureList textures;
    for (auto& name : textureNames) {
        List<String> fileNames; // must be local here so it gets reset every loop iteration
        Texture* t = FindTexture(name);
        if (not t) {
            fileNames.Append(name);
            if (not ((t = getTexture(name)) and t->CreateFromFile(textureFolder, fileNames, params))) {
#ifdef _DEBUG
                fprintf(stderr, "TextureHandler: Couldn't load texture '%s'.\n", (char*)(textureFolder + name));
#endif
                for (auto& h : textures)
                    delete h;
                textures.Clear();
                break;
            }
        }
        //t->m_id.name = n.Split('.')[0];
        textures.Append(t);
    }
    return textures;
}


TextureList TextureHandler::CreateByType(String textureFolder, List<String>& textureNames, GLenum textureType, const TextureCreationParams& params) {
    return CreateTextures(textureFolder, textureNames, [&](String& name) { return (textureType == GL_TEXTURE_CUBE_MAP) ? GetCubemap(name) : GetStandardTexture(name); }, params);
}

// =================================================================================================
