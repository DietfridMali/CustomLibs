
#include "texturehandler.h"
#include "list.hpp"
#include "noisetexture.h"
#include <format>

// =================================================================================================
// Very simple class for texture tracking
// Main purpose is to keep track of all texture objects in the game and return them to OpenGL in
// a well defined and controlled way at program termination without having to bother about releasing
// textures at a dozen places in the game


bool TextureHandler::DeleteTextures(const String& key, Texture** texture) {
    if (texture and *texture) {
        delete *texture;
        *texture = nullptr;
    }
    return true;
}


bool TextureHandler::RedeployTextures(const String& key, Texture** texture) {
    if (texture and *texture)
        (*texture)->Redeploy();
    return true;
}


void TextureHandler::Destroy(void) noexcept {
    Texture::UpdateLUT(false);
    Texture::textureLUT.Walk(&TextureHandler::DeleteTextures, this);
}


void TextureHandler::Redeploy(void) noexcept {
    Texture::textureLUT.Walk(&TextureHandler::RedeployTextures, this);
}


Texture* TextureHandler::FindTexture(String& name) {
    Texture** t = Texture::textureLUT.Find(name);
    return t ? *t : nullptr;
}


TextureList TextureHandler::CreateTextures(String textureFolder, List<String>& textureNames, TextureGetter getTexture, const TextureCreationParams& params) {
    GetTextureFolder(textureFolder);
    TextureList textures;
    for (auto& name : textureNames) {
        List<String> fileNames; // must be local here so it gets reset every loop iteration
        String key;
        if (params.keyDecoration.IsEmpty())
            key = name;
        else
            key.Format((const char*)params.keyDecoration, (const char*)name);
        Texture* t = FindTexture(key);
        if (not t) {
            fileNames.Append(key);
            if (not ((t = getTexture(name)) and t->CreateFromFile(textureFolder, fileNames, params))) {
                if (t) {
                    delete t;
                    t = nullptr;
                }
                if (params.isRequired) {
#ifdef _DEBUG
                    fprintf(stderr, "TextureHandler: Couldn't load texture '%s'.\n", (char*)(textureFolder + name));
#endif
                    for (auto& h : textures)
                        delete h;
                    textures.Clear();
                    break;
                }
            }
        }
        //t->m_id.name = n.Split('.')[0];
        if (t)
            textures.Append(t);
    }
    return textures;
}


TextureList TextureHandler::CreateByType(String textureFolder, List<String>& textureNames, TextureType textureType, const TextureCreationParams& params) {
    TextureGetter getter;
    switch (textureType) {
        case TextureType::CubeMap:   
            getter = [&](String& name) { return GetCubemap(name); }; 
            break;

        case TextureType::Texture3D:
            getter = [&](String& name) { return GetTexture<NoiseTexture3D>(name); };
            break;

        default:                     
            getter = [&](String& name) { return GetStandardTexture(name); }; 
            break;
    }
    return CreateTextures(textureFolder, textureNames, getter, params);
}

// =================================================================================================
