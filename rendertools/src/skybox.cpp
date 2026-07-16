
#include "skybox.h"
#include "cube.h"
#include "gfxstates.h"
#include "gfxrenderer.h"
#include "random.hpp"
#include "base_renderer.h"

// =================================================================================================

static List<String> skyboxDirections = { "-rt", "-lf", "-up", "-dn", "-ft", "-bk" }; 
static List<String> skyTextureSizes = { "-4k", "-2k", "-1k" };
static List<String> skyTextureTypes = { "-bright", "-medium", "-dark" };


Cubemap* Skybox::LoadTextures(const String& textureFolder, const String& baseName, const String& type, const String& size) {
	String id = baseName + type;
	Cubemap* texture = textureHandler.GetCubemap(id);
    if (not texture)
        return nullptr;

	List<String> filenames;
	for (int i = 0; i < skyboxDirections.Length(); i++) 
		filenames.Append(String::Concat(baseName, type, skyboxDirections[i], size, ".DDS"));
	
	//texture = new Cubemap();
	if (not texture->CreateFromFile(textureFolder, filenames, {})) {
		delete texture;
		return nullptr;
	}
    return texture;
}


int Skybox::MaxTextureSize(void) {
	int maxSize = gfxStates.MaxTextureSize();
	if (maxSize >= 4096)
		return 0;
	if (maxSize >= 2048)
		return 1;
	if (maxSize >= 1024)
		return 2;
	return -1;
}


bool Skybox::Setup(const String& textureFolder, CloudNoiseTexture* noiseTexture) {
	m_noiseTexture = noiseTexture;
	int textureSize = MaxTextureSize();
	if (textureSize < 0)
		return false;

	for (int i = 0; i < 3; i++) {
		if (not (m_skyTextures[0][i] = LoadTextures(textureFolder, "sky", skyTextureTypes[i], skyTextureSizes[textureSize]))) {
			while (--i >= 0)
				delete m_skyTextures[0][i];
			return false;
		}
	}

	if ((m_skyTextures[1][0] = LoadTextures(textureFolder, "starmap", "", skyTextureSizes[textureSize])))
		m_skyTextures[1][1] = m_skyTextures[1][2] = m_skyTextures[1][0];
	else {
		delete m_skyTextures[1][0];
		m_skyTextures[1][0] = nullptr;
	}

	if ((m_skyTextures[2][0] = LoadTextures(textureFolder, "nightsky", "", skyTextureSizes[textureSize])))
		m_skyTextures[2][1] = m_skyTextures[2][2] = m_skyTextures[2][0];
	else {
		delete m_skyTextures[2][0];
		m_skyTextures[2][0] = nullptr;
	}

	m_skybox = new Mesh();
	if (not m_skybox)
		return false;

	m_skybox->SetDynamic(false);
	m_skybox->Init(MeshTopology::Triangles, 1);
	
	Vector3f offset({ 0.5f, 0.5f, 0.5f }), v;
	for (int i = 0; i < Cube::vertexCount; i++) {
		v = Cube::vertices[i] - offset;
		v *= 2.0f;
		m_skybox->AddVertex(v);
	}
	AutoArray<GfxTypes::Uint> indices;
	indices.Resize(sizeof(Cube::triangleIndices) / sizeof(GfxTypes::Uint));
	memcpy(indices.DataPtr(), Cube::triangleIndices, sizeof(Cube::triangleIndices));
	m_skybox->SetIndices(indices);
	m_skybox->UpdateData();
	return true;
}


Shader* Skybox::LoadBlackholeShader(Matrix4f& view, Vector3f lightDirection, float brightness, float alpha, int32_t currentTime) {
	Shader* shader = baseShaderHandler.SetupRenderShader("blackhole");
	if (shader) {
		shader->SetMatrix4f("mView", view.AsArray(), false);
		if (baseRenderer.UsesOpenGL()) {
			shader->SetInt("sky", 0);
			shader->SetInt("noiseTex", 1);
		}
		shader->SetMatrix4f("mView", view.AsArray(), false);
		shader->SetVector3f("direction", Vector3f({ 0.0f, 0.20f, -0.99f }));   // normalisiert, horizontnah
#ifdef _DEBUG
		shader->SetFloat("distance", 20.0f + 10.0f * sinf(currentTime / 1.8e3f));
#else
		shader->SetFloat("distance", 25.0f + 5.0f * sinf(currentTime / 1.8e6f));
#endif
		shader->SetVector3f("diskNormal", Vector3f({ -0.2f, 0.8f, 0.0f }));
		shader->SetFloat("gravity", 0.95f);
		shader->SetFloat("time", float(currentTime) / 1000.0f);   // currentTime durchreichen
		shader->SetFloat("horizon", 1.0f);
		shader->SetFloat("innerDiskRad", 2.6f);
		shader->SetFloat("outerDiskRad", 9.0f);
		shader->SetFloat("angSpeed", 0.35f);
		shader->SetFloat("brightness", 1.0f); // brightness);
		shader->SetFloat("noiseScale", 0.25f);
		shader->SetVector3f("lightDirection", lightDirection);
		shader->SetFloat("brightness", brightness);
		shader->SetFloat("alpha", alpha);
	}
    return shader;
}


Shader* Skybox::LoadShader(Matrix4f& view, Vector3f lightDirection, float brightness, float alpha, int32_t currentTime) {
	Shader* shader = baseShaderHandler.SetupRenderShader("skybox");
	if (shader) {
		shader->SetMatrix4f("mView", view.AsArray(), false);
		StaticArray<String, 3> skyNames = { "sky1", "sky2", "sky3" };
		for (int i = 0; i < 3; i++) {
			if (baseRenderer.UsesOpenGL())
				shader->SetInt(skyNames[i], i);
		}
		shader->SetVector3f("lightDirection", lightDirection);
		shader->SetFloat("brightness", brightness);
		shader->SetFloat("alpha", alpha);
	}
	return shader;
}


bool Skybox::Render(int32_t skyType, Matrix4f& view, Vector3f lightDirection, float brightness, int32_t currentTime) {
	if (not m_skybox)
		return false;

	void* cl;
	if (not baseRenderer.StartOperation(&cl, "skybox"))
		return false;
	gfxStates.SetFaceCulling(0);
	gfxStates.SetDepthWrite(0);
	gfxStates.SetDepthTest(1);
	gfxStates.DepthFunc(GfxOperations::CompareFunc::LessEqual);
	float alpha = std::min(float(currentTime - m_activationTime) / 1000.0f, 1.0f);
	gfxStates.SetBlending(alpha < 1.0f ? 1 : 0);
	Shader* shader = nullptr;
	if (not HasNightSky(skyType))
		skyType = 0;
	else if (skyType == 3) {
		if ((m_noiseTexture != nullptr) and (shader = LoadBlackholeShader(view, lightDirection, 1.0f, alpha, currentTime))) {
			m_skyTextures[1][0]->Activate(0);
			m_noiseTexture->Activate(1);
			m_skybox->Render({}); // m_skyTextures);
			m_skyTextures[1][0]->Deactivate();
			m_noiseTexture->Deactivate();
		}
		else {
			skyType = 1;
		}
	}
	if (skyType != 3) {
		if ((shader = LoadShader(view, lightDirection, skyType ? 0.7f : brightness, alpha, currentTime))) {
			for (int i = 0; i < 3; i++)
				m_skyTextures[skyType][i]->Activate(i);
			m_skybox->Render({}); // m_skyTextures);
			for (int i = 0; i < 3; i++)
				m_skyTextures[skyType][i]->Deactivate();
		}
	}
	baseRenderer.FinishOperation(cl);
	return shader != nullptr;
};

// =================================================================================================

