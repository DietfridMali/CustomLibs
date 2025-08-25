#pragma once 
#include "texture.h"

// =================================================================================================
// Load cubemap textures from file and generate an OpenGL cubemap 

class Cubemap : public Texture {
public:
    Cubemap() : Texture(0, GL_TEXTURE_CUBE_MAP) {}

    virtual void SetParams(void);

    virtual void Deploy(int bufferIndex = 0);

};

// =================================================================================================
