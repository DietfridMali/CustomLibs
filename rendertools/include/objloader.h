#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>

#include "string.hpp"
#include "vector.hpp"
#include "array.hpp"
#include "colordata.h"
#include "texcoord.h"
#include "rendertypes.h"
#include "glbloader.h"

// =================================================================================================

class ObjLoader {
public:
	static constexpr float AlphaCutoff = 0.5f;

	static bool IsObjFile(const String& filename);

	bool Load(const String& filename);

	inline GLBLoader::MeshData& Data() {
		return m_data;
	}

	inline const GLBLoader::MeshData& Data() const {
		return m_data;
	}

	void Reset(void);

	~ObjLoader() {
		Reset();
	}

private:
	struct MaterialInfo {
		std::string				name;
		RGBAColor				color{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::filesystem::path	texture;
		std::filesystem::path	alphaTexture;
		GfxWrapMode				wrap{ GfxWrapMode::Repeat };
		bool					hasDissolve{ false };
	};

	struct Corner {
		int32_t position{ -1 };
		int32_t texCoord{ -1 };
		int32_t normal{ -1 };
	};

	GLBLoader::MeshData					m_data;
	AutoArray<Vector3f>					m_positions;
	AutoArray<RGBAColor>				m_positionColors;
	AutoArray<TexCoord>					m_texCoords;
	AutoArray<Vector3f>					m_normals;
	AutoArray<MaterialInfo>				m_materials;
	AutoArray<std::filesystem::path>	m_imageFiles;
	AutoArray<Corner>					m_corners;
	GLBLoader::PartData					m_part;

	std::map<std::tuple<int32_t, int32_t, int32_t>, int32_t> m_cornerVertices;

	void ReleaseSource(void);

	bool ParseFile(const std::filesystem::path& filename);

	void LoadMaterialLibraries(const std::filesystem::path& folder, const std::string& arguments);

	void LoadMaterials(const std::filesystem::path& filename);

	int32_t FindMaterial(const std::string& name) const;

	void UseMaterial(const std::string& name);

	bool AddPosition(const std::string& arguments);

	bool AddTexCoord(const std::string& arguments);

	bool AddNormal(const std::string& arguments);

	bool AddFace(const std::string& arguments);

	bool ParseCorner(const std::string& token, Corner& corner) const;

	void AddTriangle(const Corner& c0, const Corner& c1, const Corner& c2);

	int32_t CornerVertex(const Corner& corner, const RGBAColor& materialColor);

	void FinishPart(void);

	void BuildMaterials(void);

	int32_t ImageIndex(const std::filesystem::path& filename);
};

// =================================================================================================
