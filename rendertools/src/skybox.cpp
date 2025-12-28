
#include "skybox.h"
#include "cube.h"
#include "opengl_states.h"
#include "tristate.h"

// =================================================================================================

static List<String> skyboxDirections = { "lf", "rt", "up", "dn", "ft", "bk" }; 
static List<String> skyTextureSizes = { "-4k", "-2k", "-1k" };
static List<String> skyTextureTypes = { "bright-", "medium-", "dark-" };


Cubemap* Skybox::LoadTextures(const String& textureFolder, const String& type, const String& size) {
	String id = String("skybox-") + type;
	Cubemap* texture = textureHandler.GetCubemap(id);
    if (not texture)
        return nullptr;

	List<String> filenames;
	for (int i = 0; i < skyboxDirections.Length(); i++) 
		filenames.Append(String::Concat("sky-", type, skyboxDirections[i], size, ".png"));
	
	texture = new Cubemap();
	if (not texture->CreateFromFile(textureFolder, filenames, {})) {
		delete texture;
		return nullptr;
	}
    return texture;
}


int Skybox::MaxTextureSize(void) {
	int maxSize = openGLStates.MaxTextureSize();
	if (maxSize >= 4096)
		return 0;
	if (maxSize >= 2048)
		return 1;
	if (maxSize >= 1024)
		return 2;
	return -1;
}


bool Skybox::Setup(const String& textureFolder) {
	int textureSize = MaxTextureSize();
	if (textureSize < 0)
		return false;

	for (int i = 0; i < 3; i++) {
		if (not (m_skyTextures[i] = LoadTextures(textureFolder, skyTextureTypes[i], skyTextureSizes[i]))) {
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


bool Skybox::Render(Matrix4f& view, Vector3f lightDirection, float brightness) {
	if (not m_skybox)
		return false;

	Shader* shader = LoadShader(view, lightDirection, brightness);
	if (not shader)
		return false;

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
	return true;
};

// =================================================================================================

