
#include "skybox.h"
#include "cube.h"
#include "gfxstates.h"
#include "gfxrenderer.h"
#include "random.hpp"
#include "base_renderer.h"
#include "texturebuffer.h"
#include "loghandler.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26819)
#endif
#include "SDL_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// =================================================================================================

static List<String> skyboxDirections = { "-rt", "-lf", "-up", "-dn", "-ft", "-bk" };
static List<String> skyTextureSizes = { "-4k", "-2k", "-1k" };
static List<String> skyTextureTypes = { "-bright", "-medium", "-dark" };


Cubemap* Skybox::LoadTextures(const String& textureFolder, const String& baseName, const String& type, const String& size)
{
	String		id = baseName + type;
	Cubemap*	texture = textureHandler.GetCubemap(id);
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


int Skybox::MaxTextureSize(int maxTextureSize)
{
	int maxSize = gfxStates.MaxTextureSize();
	if ((maxTextureSize > 0) and (maxTextureSize < maxSize))
		maxSize = maxTextureSize;
	if (maxSize >= 4096)
		return 0;
	if (maxSize >= 2048)
		return 1;
	if (maxSize >= 1024)
		return 2;
	return -1;
}


bool Skybox::Setup(const String& textureFolder, CloudNoiseTexture* noiseTexture, Texture* blueNoise, int maxTextureSize)
{
	m_textureFolder = textureFolder;
	m_noiseTexture = noiseTexture;
	m_blueNoise = blueNoise;
	int textureSize = MaxTextureSize(maxTextureSize);
	if (textureSize < 0)
		return false;

	for (int i = 0; i < 3; i++) {
		if (not (m_skyTextures[0][i] = LoadTextures(textureFolder, "sky", skyTextureTypes[i], skyTextureSizes[textureSize]))) {
			while (--i >= 0) {
				delete m_skyTextures[0][i];
				m_skyTextures[0][i] = nullptr;
			}
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


// The night sky and star map entries alias one texture across all three slots (Setup ()), so a
// straight loop over the array would delete the same object three times - collect what is there and
// clear every reference to it first.

void Skybox::Destroy(void)
{
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			Cubemap* texture = m_skyTextures[i][j];
			if (texture == nullptr)
				continue;
			for (int k = j; k < 3; k++)
				if (m_skyTextures[i][k] == texture)
					m_skyTextures[i][k] = nullptr;
			delete texture;
		}
	}
	if (m_skybox != nullptr) {
		delete m_skybox;
		m_skybox = nullptr;
	}
	m_activationTime = -1;
}


Cubemap* Skybox::LoadCubemap(const String& textureFolder, String id, List<String>& filenames)
{
	Cubemap* texture = textureHandler.GetCubemap(id);
	if (not texture)
		return nullptr;
	if (not texture->CreateFromFile(textureFolder, filenames, { .isRequired = false })) {
		delete texture;
		return nullptr;
	}
	return texture;
}


bool Skybox::SaveFaces(Cubemap* texture, List<String>& filenames)
{
	bool	ok = true;
	int		face = 0;
	for (auto& filename : filenames) {
		TextureBuffer*	buffer = texture->m_buffers[face++];
		String			path = m_textureFolder + filename;
		int				width = buffer->m_info.m_width;
		int				height = buffer->m_info.m_height;
		SDL_Surface*	surface =
			SDL_CreateRGBSurfaceWithFormatFrom(buffer->DataBuffer(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
		if (not surface) {
			logHandler.Print("Skybox: cannot write '%s': %s\n", static_cast<const char*>(path), SDL_GetError());
			ok = false;
			continue;
		}
		if (IMG_SavePNG(surface, static_cast<const char*>(path)) != 0) {
			logHandler.Print("Skybox: cannot write '%s': %s\n", static_cast<const char*>(path), SDL_GetError());
			ok = false;
		}
		SDL_FreeSurface(surface);
	}
	return ok;
}


bool Skybox::ApplyKuwaharaFilter(int32_t skyType, const KuwaharaFilter::Params& params, const String& suffix, const String& sourceFolder)
{
	KuwaharaFilter	kuwaharaFilter;
	String			filteredExtension = suffix + ".png";
	bool			ok = true;
	for (int j = 0; j < 3; j++) {
		Cubemap* texture = m_skyTextures[skyType][j];
		if ((texture == nullptr) or ((j > 0) and (texture == m_skyTextures[skyType][j - 1])))
			continue;
		List<String> filteredNames;
		List<String> sourceNames;
		for (auto& filename : texture->m_filenames) {
			filteredNames.Append(filename.Replace(".DDS", static_cast<const char*>(filteredExtension)));
			sourceNames.Append(filename.Replace(".DDS", ".png"));
		}
		String		id = texture->m_name + suffix;
		Cubemap*	filtered = LoadCubemap(m_textureFolder, id, filteredNames);
		if (not filtered) {
			filtered = LoadCubemap(sourceFolder, id, sourceNames);
			if (not filtered) {
#ifdef _DEBUG
				logHandler.Print("Skybox: sources of '%s' not found in '%s', filtering the loaded sky\n",
								 static_cast<const char*>(texture->m_name), static_cast<const char*>(sourceFolder));
#endif
				if (not kuwaharaFilter.ApplyCube(texture, params))
					ok = false;
				continue;
			}
			if (not kuwaharaFilter.ApplyCube(filtered, params)) {
				delete filtered;
				ok = false;
				continue;
			}
			if (not SaveFaces(filtered, filteredNames))
				ok = false;
		}
		for (int k = j; k < 3; k++)
			if (m_skyTextures[skyType][k] == texture)
				m_skyTextures[skyType][k] = filtered;
		delete texture;
	}
	return ok;
}


Shader* Skybox::LoadBlackholeShader(Matrix4f& view, Vector3f lightDirection, float brightness, float alpha, int32_t currentTime)
{
	Shader* shader = baseShaderHandler.SetupRenderShader("blackhole");
	if (shader) {
		shader->SetMatrix4f("mView", view.AsArray(), false);
		if (baseRenderer.UsesOpenGL()) {
			shader->SetInt("sky", 0);
			shader->SetInt("noiseTex", 1);
			shader->SetInt("blueNoiseTex", 2);
		}
		shader->SetMatrix4f("mView", view.AsArray(), false);
		shader->SetVector3f("direction", Vector3f({ 0.0f, 0.20f, -0.99f })); // normalisiert, horizontnah
#ifdef _DEBUG
		shader->SetFloat("distance", 20.0f + 10.0f * sinf(currentTime / 1.8e4f));
#else
		shader->SetFloat("distance", 25.0f + 5.0f * sinf(currentTime / 1.8e6f));
#endif
		shader->SetVector3f("diskNormal", Vector3f({ -0.2f, 0.8f, 0.0f }));
		shader->SetFloat("gravity", 0.95f);
		shader->SetFloat("time", float(currentTime) / 1000.0f); // currentTime durchreichen
		shader->SetFloat("horizon", 1.0f);
		shader->SetFloat("innerDiskRad", 2.6f);
		shader->SetFloat("outerDiskRad", 9.0f);
		shader->SetFloat("angSpeed", 0.35f);
		shader->SetFloat("brightness", 1.0f); // brightness);
		shader->SetFloat("noiseScale", 0.25f);
		shader->SetVector3f("lightDirection", lightDirection);
		shader->SetFloat("brightness", brightness * 6.0f);
		shader->SetFloat("alpha", alpha);
	}
	return shader;
}


Shader* Skybox::LoadShader(Matrix4f& view, Vector3f lightDirection, float brightness, float alpha, int32_t currentTime)
{
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


bool Skybox::Render(int32_t skyType, Matrix4f& view, Vector3f lightDirection, float brightness, int32_t currentTime)
{
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
			if (m_blueNoise != nullptr)
				m_blueNoise->Activate(2);
			m_skybox->Render({}); // m_skyTextures);
			m_skyTextures[1][0]->Deactivate();
			m_noiseTexture->Deactivate();
			if (m_blueNoise != nullptr)
				m_blueNoise->Deactivate();
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
