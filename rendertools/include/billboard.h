#pragma once

#include "base_quadmesh.h"
#include "texture.h"

// =================================================================================================

class Billboard
	: public BaseQuadMesh {
protected:
	Texture* m_icon;

public:
	bool Setup(String textureFolder, String iconName);

	void Update(Vector3f p0, Vector3f p1, Vector3f p2, float width = 1.0f, float height = 1.0f, float offset = 0.0f);

	void Render(void);

	bool Setup(std::initializer_list<Vector3f> vertices, std::initializer_list<TexCoord> texCoords, bool privateGfxData) override {
		return BaseQuadMesh::Setup(vertices, texCoords, privateGfxData);
	}

	bool Update(void) override {
		return Mesh::Update();
	}

	bool Render(std::span<Texture* const> textures, float alpha) override {
		return BaseQuadMesh::Render(textures, alpha);
	}
};

// =================================================================================================
