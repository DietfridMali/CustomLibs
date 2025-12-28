
#include "skybox.h"
#include "cube.h"
#include "opengl_states.h"
#include "tristate.h"

// =================================================================================================

bool Skybox::LoadTextures(const String& textureFolder) {
	static List<String> fileNames = {
		"skybox-right.png",
		"skybox-left.png",
		"skybox-top.png",
		"skybox-bottom.png",
		"skybox-front.png",
		"skybox-back.png"
	};
	String id = "skybox";
    m_texture = textureHandler.GetCubemap(id);
    if (not m_texture)
        return false;
    if (not m_texture->CreateFromFile(textureFolder, fileNames, {}))
        return false;
    return true;
}


bool Skybox::Create(const String& textureFolder) {
	if (not LoadTextures(textureFolder))
		return false;

	m_skybox = new Mesh();
	if (not m_skybox)
		return false;

	m_skybox->SetDynamic(false);
	m_skybox->Init(GL_TRIANGLES, 1);
	
	Vector3f offset({ 0.5f, 0.5f, 0.5f }), v;
	for (int i = 0; i < Cube::vertexCount; i++) {
		v = Cube::vertices[i] - offset;
		v *= 2.0f;
		m_skybox->AddVertex(v);
	}
	ManagedArray<GLuint> indices;
	indices.Resize(sizeof(Cube::triangleIndices) / sizeof(GLuint));
	memcpy(indices.Data(), Cube::triangleIndices, sizeof(Cube::triangleIndices));
	m_skybox->SetIndices(indices);
	m_skybox->UpdateVAO();
	return true;
}


Shader* Skybox::LoadShader(Matrix4f& view) {
    Shader* shader = baseShaderHandler.SetupShader("skybox");
    if (shader) {
        shader->SetMatrix4f("view", view.AsArray(), false);
    }
    return shader;
}


void Skybox::Render(Matrix4f& view) {
	if (m_texture and m_skybox) {
		Shader* shader = LoadShader(view);
		if (shader) {
			Tristate<int> faceCulling(-1, 1, openGLStates.SetFaceCulling(0));
			Tristate<int> depthWrite(-1, 1, openGLStates.SetDepthWrite(0));
			Tristate<GLenum> depthFunc(GL_NONE, GL_LEQUAL, openGLStates.DepthFunc(GL_ALWAYS));
			m_skybox->Render(m_texture);
			openGLStates.DepthFunc(depthFunc);
			openGLStates.SetDepthWrite(depthWrite);
			openGLStates.SetFaceCulling(faceCulling);
		}
	}
};

// =================================================================================================

