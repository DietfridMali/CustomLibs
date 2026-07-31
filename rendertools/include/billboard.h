#pragma once

#include "base_quadmesh.h"
#include "texture.h"

// =================================================================================================

class Billboard 
	: public BaseQuadMesh
{
protected:
	Texture* m_icon;

public:
	bool Setup(String textureFolder, String iconName);

	void Update(Vector3f p0, Vector3f p1, Vector3f p2, float width = 1.0f, float height = 1.0f, float offset = 0.0f);

	void Render(void);
};

// =================================================================================================

