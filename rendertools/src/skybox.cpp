
#include "skybox.h"
#include "cube.h"
#include "opengl_states.h"
#include "tristate.h"

// =================================================================================================

static List<String> skyFilenames[3] = {
	{ "bright-sky-lf.png", "bright-sky-rt.png", "bright-sky-up.png", "bright-sky-dn.png", "bright-sky-ft.png", "bright-sky-bk.png" },
	{ "medium-sky-lf.png", "medium-sky-rt.png", "medium-sky-up.png", "medium-sky-dn.png", "medium-sky-ft.png", "medium-sky-bk.png" },
	{ "dark-sky-lf.png", "dark-sky-rt.png", "dark-sky-up.png", "dark-sky-dn.png", "dark-sky-ft.png", "dark-sky-bk.png" }
};

Cubemap* Skybox::LoadTextures(const String& textureFolder, List<String>& filenames) {
	String id = "skybox";
    Cubemap* texture = textureHandler.GetCubemap(id);
    if (not texture)
        return nullptr;
	if (not texture->CreateFromFile(textureFolder, filenames, {})) {
		delete texture;
		return nullptr;
	}
    return texture;
}


bool Skybox::Setup(const String& textureFolder) {
	for (int i = 0; i < 3; i++) {
		if (not (m_skyTextures[i] = LoadTextures(textureFolder, skyFilenames[i]))) {
			while (--i >= 0)
				delete m_skyTextures[i];
			return false;
		}
	}
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


Shader* Skybox::LoadShader(Matrix4f& view, Vector3f lightDirection, float brightness) {
    Shader* shader = baseShaderHandler.SetupShader("skybox");
    if (shader) {
		static String uSkyNames[3] = { "sky1", "sky2", "sky3" };
        shader->SetMatrix4f("mView", view.AsArray(), false);
		for (int i = 0; i < 3; i++) {
			m_skyTextures[i]->Enable(i);
			shader->SetInt(uSkyNames[i], i);
			}
		shader->SetVector3f("lightDirection", lightDirection);
		shader->SetFloat("brightness", brightness);
    }
    return shader;
}


void Skybox::Render(Matrix4f& view, Vector3f lightDirection, float brightness) {
	if (m_skybox) {
		Shader* shader = LoadShader(view, lightDirection, brightness);
		if (shader) {
			Tristate<int> faceCulling(-1, 1, openGLStates.SetFaceCulling(0));
			Tristate<int> depthWrite(-1, 1, openGLStates.SetDepthWrite(0));
			Tristate<GLenum> depthFunc(GL_NONE, GL_LEQUAL, openGLStates.DepthFunc(GL_ALWAYS));
			for (int i = 0; i < 3; i++)
				m_skyTextures[i]->Enable(i);
			m_skybox->Render(nullptr);
			for (int i = 0; i < 3; i++)
				m_skyTextures[i]->Release(i);
			openGLStates.DepthFunc(depthFunc);
			openGLStates.SetDepthWrite(depthWrite);
			openGLStates.SetFaceCulling(faceCulling);
		}
	}
};

// =================================================================================================

