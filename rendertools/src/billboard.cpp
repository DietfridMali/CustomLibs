
#include "billboard.h"
#include "texturehandler.h"

// =================================================================================================

bool Billboard::Setup(String iconName) {
	List<String> filename = { iconName };
	TextureList	texture = textureHandler.CreateStandardTextures("", filename, {});
	if (texture.IsEmpty())
		return false;
	m_icon = texture[0];
	return true;
}


void Billboard::Update(Vector3f p0, Vector3f p1, Vector3f p2, float width, float height, float offset) {
	Vector3f u = p1 - p2;
	u.Normalize();
	Vector3f f = p1 - p0;
	f.Normalize();
	Vector3f h = u.Cross(f);
	h.Normalize();
	Vector3f v = f.Cross(h);
	v.Normalize();
	v *= height;
	h *= width;
	if (offset)
		p1 += f * offset;
	BaseQuad::Setup({ p1 - h - v, p1 - h + v, p1 + h + v, p1 + h - v });
}


void  Billboard::Render(void) {
	gfxStates.SetFaceCulling(0);
	gfxStates.SetBlending(1);
	//SetTransformations({ .centerOrigin = true });
	BaseQuad::Render(nullptr, m_icon);
}

// =================================================================================================

