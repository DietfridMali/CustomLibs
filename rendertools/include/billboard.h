#pragma once

#include "base_quad.h"
#include "texture.h"

// =================================================================================================

class Billboard 
	: public BaseQuad
{
protected:
	Texture* m_icon;

public:
	bool Setup(String iconName);

	void Update(Vector3f p0, Vector3f p1, Vector3f p2, float width = 1.0f, float height = 1.0f);

	void Render(void);
};

// =================================================================================================

