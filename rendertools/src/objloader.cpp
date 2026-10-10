#include "objloader.h"
#include "loghandler.h"
#include "missingfiles.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

// =================================================================================================

static RGBAColor Modulate(const RGBAColor& a, const RGBAColor& b)
{
	return RGBAColor(a.R() * b.R(), a.G() * b.G(), a.B() * b.B(), a.A() * b.A());
}


static std::string Trim(const std::string& s, size_t from = 0)
{
	size_t start = s.find_first_not_of(" \t", from);
	if (start == std::string::npos)
		return std::string();
	size_t end = s.find_last_not_of(" \t");
	return s.substr(start, end - start + 1);
}


static std::string NextToken(const std::string& s, size_t& pos)
{
	size_t start = s.find_first_not_of(" \t", pos);
	if (start == std::string::npos) {
		pos = std::string::npos;
		return std::string();
	}
	pos = s.find_first_of(" \t", start);
	return s.substr(start, (pos == std::string::npos) ? std::string::npos : pos - start);
}


static std::string PortablePath(const std::string& name)
{
	std::string path = name;
	std::replace(path.begin(), path.end(), '\\', '/');
	return path;
}


static bool ReadLine(std::ifstream& file, std::string& line, int32_t& lineNumber)
{
	line.clear();
	std::string part;
	while (std::getline(file, part)) {
		++lineNumber;
		if (not part.empty() and (part.back() == '\r'))
			part.pop_back();
		if (part.empty() or (part.back() != '\\')) {
			line += part;
			return true;
		}
		part.back() = ' ';
		line += part;
	}
	return not line.empty();
}


static bool SplitStatement(const std::string& line, std::string& keyword, std::string& arguments)
{
	size_t pos = 0;
	keyword = NextToken(line, pos);
	if (keyword.empty() or (keyword[0] == '#'))
		return false;
	std::transform(keyword.begin(), keyword.end(), keyword.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	arguments = Trim(line, pos);
	return true;
}


static int32_t ParseFloats(const std::string& arguments, float* values, int32_t maxCount)
{
	std::istringstream	stream(arguments);
	int32_t				count = 0;
	while ((count < maxCount) and (stream >> values[count]))
		++count;
	return count;
}


static bool ParseLastFloat(const std::string& arguments, float& value)
{
	size_t				start = arguments.find_last_of(" \t");
	std::istringstream	stream((start == std::string::npos) ? arguments : arguments.substr(start + 1));
	return static_cast<bool>(stream >> value);
}


static bool IsNumber(const std::string& token)
{
	std::istringstream	stream(token);
	float				value;
	return static_cast<bool>(stream >> value) and (stream.peek() == EOF);
}


static int32_t TextureOptionArgCount(const std::string& option)
{
	static const char* singleArgOptions[] = { "-blendu", "-blendv", "-bm", "-boost", "-cc", "-clamp", "-imfchan", "-texres", "-type" };
	for (const char* name : singleArgOptions)
		if (option == name)
			return 1;
	if (option == "-mm")
		return 2;
	if ((option == "-o") or (option == "-s") or (option == "-t"))
		return 3;
	return 0;
}


static bool ParseTextureStatement(const std::string& arguments, const std::filesystem::path& folder,
								  std::filesystem::path& texture, GfxWrapMode& wrap)
{
	size_t pos = 0;
	for (;;) {
		size_t start = arguments.find_first_not_of(" \t", pos);
		if (start == std::string::npos)
			return false;
		pos = start;
		std::string	option = NextToken(arguments, pos);
		int32_t		argCount = TextureOptionArgCount(option);
		if (argCount == 0) {
			texture = folder / std::filesystem::path(PortablePath(Trim(arguments, start)));
			return true;
		}
		for (int32_t i = 0; i < argCount; ++i) {
			size_t		argPos = pos;
			std::string	value = NextToken(arguments, argPos);
			if (value.empty() or ((argCount == 3) and (i > 0) and not IsNumber(value)))
				break;
			pos = argPos;
			if (option == "-clamp")
				wrap = (value == "on") ? GfxWrapMode::ClampToEdge : GfxWrapMode::Repeat;
		}
	}
}


static int32_t ResolveIndex(int32_t index, int32_t count)
{
	int32_t i = (index > 0) ? index - 1 : count + index;
	return ((i >= 0) and (i < count)) ? i : -1;
}

// =================================================================================================

bool ObjLoader::IsObjFile(const String& filename)
{
	std::ifstream	file(static_cast<const char*>(filename), std::ios::binary);
	char			magic[4] = {};
	file.read(magic, sizeof(magic));
	if (file.good() and (memcmp(magic, "glTF", sizeof(magic)) == 0))
		return false;
	file.clear();
	file.seekg(0);
	int c = file.get();
	if (c == 0xEF) {
		file.ignore(2);
		c = file.get();
	}
	while ((c != EOF) and std::isspace(c))
		c = file.get();
	return (c != EOF) and (c != '{');
}


void ObjLoader::Reset(void)
{
	m_data.vertices.Clear();
	m_data.colors.Clear();
	m_data.normals.Clear();
	m_data.texCoords.Clear();
	m_data.shapeKeys.Clear();
	m_data.indices.Clear();
	m_data.parts.Clear();
	m_data.materials.Clear();
	m_data.images.Clear();
	m_data.imageNames.Clear();
	ReleaseSource();
}


void ObjLoader::ReleaseSource(void)
{
	m_positions.Clear();
	m_positionColors.Clear();
	m_texCoords.Clear();
	m_normals.Clear();
	m_materials.Clear();
	m_imageFiles.Clear();
	m_corners.Clear();
	m_cornerVertices.clear();
	m_part = GLBLoader::PartData();
}


bool ObjLoader::Load(const String& filename)
{
	Reset();
	if (not ParseFile(std::filesystem::path(static_cast<const char*>(filename)))) {
		Reset();
		return false;
	}
	BuildMaterials();
	ReleaseSource();
	return true;
}


bool ObjLoader::ParseFile(const std::filesystem::path& filename)
{
	std::ifstream file(filename);
	if (not file) {
		logHandler.Print("ObjLoader: cannot open '%s'\n", filename.string().c_str());
		missingFiles.Report(filename.string().c_str());
		return false;
	}
	std::filesystem::path	folder = filename.parent_path();
	std::string				line;
	std::string				keyword;
	std::string				arguments;
	int32_t					lineNumber = 0;
	while (ReadLine(file, line, lineNumber)) {
		if (not SplitStatement(line, keyword, arguments))
			continue;
		bool isValid = true;
		if (keyword == "v")
			isValid = AddPosition(arguments);
		else if (keyword == "vt")
			isValid = AddTexCoord(arguments);
		else if (keyword == "vn")
			isValid = AddNormal(arguments);
		else if (keyword == "f")
			isValid = AddFace(arguments);
		else if (keyword == "usemtl")
			UseMaterial(arguments);
		else if (keyword == "mtllib")
			LoadMaterialLibraries(folder, arguments);
		if (not isValid) {
			logHandler.Print("ObjLoader: %s (%d): invalid '%s' statement\n", filename.string().c_str(), lineNumber, keyword.c_str());
			return false;
		}
	}
	FinishPart();
	return true;
}


void ObjLoader::LoadMaterialLibraries(const std::filesystem::path& folder, const std::string& arguments)
{
	std::string		names = PortablePath(arguments);
	std::error_code	error;
	if (std::filesystem::exists(folder / std::filesystem::path(names), error)) {
		LoadMaterials(folder / std::filesystem::path(names));
		return;
	}
	size_t pos = 0;
	for (std::string name = NextToken(names, pos); not name.empty(); name = NextToken(names, pos))
		LoadMaterials(folder / std::filesystem::path(name));
}


void ObjLoader::LoadMaterials(const std::filesystem::path& filename)
{
	std::ifstream file(filename);
	if (not file) {
		logHandler.Print("ObjLoader: cannot open material library '%s'\n", filename.string().c_str());
		missingFiles.Report(filename.string().c_str());
		return;
	}
	std::filesystem::path	folder = filename.parent_path();
	std::string				line;
	std::string				keyword;
	std::string				arguments;
	int32_t					lineNumber = 0;
	int32_t					current = -1;
	while (ReadLine(file, line, lineNumber)) {
		if (not SplitStatement(line, keyword, arguments))
			continue;
		if (keyword == "newmtl") {
			current = m_materials.Length();
			m_materials.Append()->name = arguments;
			continue;
		}
		if (current < 0)
			continue;
		MaterialInfo&	material = m_materials[current];
		float			values[3];
		if (keyword == "kd") {
			int32_t count = ParseFloats(arguments, values, 3);
			if (count == 0)
				continue;
			if (count < 3) {
				values[1] = values[0];
				values[2] = values[0];
			}
			material.color = RGBAColor(values[0], values[1], values[2], material.color.A());
		}
		else if (keyword == "d") {
			if (not ParseLastFloat(arguments, values[0]))
				continue;
			material.color = RGBAColor(material.color.R(), material.color.G(), material.color.B(), values[0]);
			material.hasDissolve = true;
		}
		else if (keyword == "tr") {
			if (material.hasDissolve or not ParseLastFloat(arguments, values[0]))
				continue;
			material.color = RGBAColor(material.color.R(), material.color.G(), material.color.B(), 1.0f - values[0]);
		}
		else if (keyword == "map_kd") {
			if (not ParseTextureStatement(arguments, folder, material.texture, material.wrap))
				logHandler.Print("ObjLoader: %s (%d): invalid 'map_Kd' statement\n", filename.string().c_str(), lineNumber);
		}
		else if (keyword == "map_d") {
			GfxWrapMode alphaWrap = material.wrap;
			if (not ParseTextureStatement(arguments, folder, material.alphaTexture, alphaWrap))
				logHandler.Print("ObjLoader: %s (%d): invalid 'map_d' statement\n", filename.string().c_str(), lineNumber);
		}
	}
}


int32_t ObjLoader::FindMaterial(const std::string& name) const
{
	for (int32_t i = 0; i < m_materials.Length(); ++i)
		if (m_materials[i].name == name)
			return i;
	return -1;
}


void ObjLoader::UseMaterial(const std::string& name)
{
	int32_t material = FindMaterial(name);
	if (material < 0)
		logHandler.Print("ObjLoader: unknown material '%s'\n", name.c_str());
	if (material == m_part.materialIndex)
		return;
	FinishPart();
	m_part.materialIndex = material;
}


bool ObjLoader::AddPosition(const std::string& arguments)
{
	float	values[6];
	int32_t	count = ParseFloats(arguments, values, 6);
	if (count < 3)
		return false;
	m_positions.Append(Vector3f(values[0], values[1], values[2]));
	m_positionColors.Append((count == 6) ? RGBAColor(values[3], values[4], values[5], 1.0f) : RGBAColor(1.0f, 1.0f, 1.0f, 1.0f));
	return true;
}


bool ObjLoader::AddTexCoord(const std::string& arguments)
{
	float values[2] = { 0.0f, 0.0f };
	if (ParseFloats(arguments, values, 2) == 0)
		return false;
	m_texCoords.Append(TexCoord(values[0], 1.0f - values[1]));
	return true;
}


bool ObjLoader::AddNormal(const std::string& arguments)
{
	float values[3];
	if (ParseFloats(arguments, values, 3) < 3)
		return false;
	Vector3f normal(values[0], values[1], values[2]);
	normal.Normalize();
	m_normals.Append(normal);
	return true;
}


bool ObjLoader::ParseCorner(const std::string& token, Corner& corner) const
{
	int32_t	values[3] = { 0, 0, 0 };
	size_t	start = 0;
	for (int32_t i = 0; i < 3; ++i) {
		size_t				end = token.find('/', start);
		std::string_view	field(token.data() + start, ((end == std::string::npos) ? token.size() : end) - start);
		if (not field.empty()) {
			std::from_chars_result result = std::from_chars(field.data(), field.data() + field.size(), values[i]);
			if ((result.ec != std::errc()) or (result.ptr != field.data() + field.size()))
				return false;
		}
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	corner.position = ResolveIndex(values[0], m_positions.Length());
	corner.texCoord = (values[1] == 0) ? -1 : ResolveIndex(values[1], m_texCoords.Length());
	corner.normal = (values[2] == 0) ? -1 : ResolveIndex(values[2], m_normals.Length());
	return (corner.position >= 0) and ((values[1] == 0) or (corner.texCoord >= 0)) and ((values[2] == 0) or (corner.normal >= 0));
}


bool ObjLoader::AddFace(const std::string& arguments)
{
	m_corners.Clear();
	size_t pos = 0;
	for (std::string token = NextToken(arguments, pos); not token.empty(); token = NextToken(arguments, pos)) {
		Corner corner;
		if (not ParseCorner(token, corner))
			return false;
		m_corners.Append(corner);
	}
	if (m_corners.Length() < 3)
		return false;
	for (int32_t i = 1; i + 1 < m_corners.Length(); ++i)
		AddTriangle(m_corners[0], m_corners[i], m_corners[i + 1]);
	return true;
}


void ObjLoader::AddTriangle(const Corner& c0, const Corner& c1, const Corner& c2)
{
	RGBAColor		materialColor = (m_part.materialIndex < 0) ? RGBAColor(1.0f, 1.0f, 1.0f, 1.0f) : m_materials[m_part.materialIndex].color;
	const Corner*	corners[3] = { &c0, &c1, &c2 };
	bool			haveNormals = true;
	for (const Corner* corner : corners) {
		if (corner->normal < 0)
			haveNormals = false;
	}
	if (haveNormals) {
		for (const Corner* corner : corners)
			m_data.indices.Append(uint32_t(CornerVertex(*corner, materialColor)));
		return;
	}
	int32_t first = m_data.vertices.Length();
	for (const Corner* corner : corners) {
		m_data.vertices.Append(m_positions[corner->position]);
		m_data.colors.Append(Modulate(materialColor, m_positionColors[corner->position]));
		m_data.texCoords.Append((corner->texCoord < 0) ? TexCoord(0.0f, 0.0f) : m_texCoords[corner->texCoord]);
	}
	Vector3f normal = Vector3f::Normal(m_data.vertices[first], m_data.vertices[first + 1], m_data.vertices[first + 2]);
	for (int32_t i = 0; i < 3; ++i) {
		m_data.normals.Append(normal);
		m_data.indices.Append(uint32_t(first + i));
	}
}


int32_t ObjLoader::CornerVertex(const Corner& corner, const RGBAColor& materialColor)
{
	std::tuple<int32_t, int32_t, int32_t>	key(corner.position, corner.texCoord, corner.normal);
	auto									found = m_cornerVertices.find(key);
	if (found != m_cornerVertices.end())
		return found->second;
	int32_t index = m_data.vertices.Length();
	m_data.vertices.Append(m_positions[corner.position]);
	m_data.colors.Append(Modulate(materialColor, m_positionColors[corner.position]));
	m_data.texCoords.Append((corner.texCoord < 0) ? TexCoord(0.0f, 0.0f) : m_texCoords[corner.texCoord]);
	m_data.normals.Append(m_normals[corner.normal]);
	m_cornerVertices.emplace(key, index);
	return index;
}


void ObjLoader::FinishPart(void)
{
	m_part.vertexCount = m_data.vertices.Length() - m_part.firstVertex;
	m_part.indexCount = m_data.indices.Length() - m_part.firstIndex;
	if (m_part.vertexCount > 0)
		m_data.parts.Append(m_part);
	m_part.firstVertex = m_data.vertices.Length();
	m_part.firstIndex = m_data.indices.Length();
	m_part.vertexCount = 0;
	m_part.indexCount = 0;
	m_cornerVertices.clear();
}


void ObjLoader::BuildMaterials(void)
{
	m_data.materials.Resize(m_materials.Length());
	for (int32_t i = 0; i < m_materials.Length(); ++i) {
		const MaterialInfo&			source = m_materials[i];
		GLBLoader::MaterialData&	material = m_data.materials[i];
		material.wrapU = source.wrap;
		material.wrapV = source.wrap;
		material.alphaCutoff = ((not source.alphaTexture.empty()) or (source.color.A() < 1.0f)) ? AlphaCutoff : 0.0f;
		material.doubleSided = false;
		if (not source.texture.empty())
			material.imageIndex = ImageIndex(source.texture);
#ifdef _DEBUG
		if ((not source.alphaTexture.empty()) and (source.alphaTexture != source.texture))
			logHandler
				.Print("ObjLoader: material '%s': separate alpha map '%s' not supported, using the alpha channel of map_Kd\n",
					   source.name.c_str(), source.alphaTexture.string().c_str());
#endif
	}
}


int32_t ObjLoader::ImageIndex(const std::filesystem::path& filename)
{
	for (int32_t i = 0; i < m_imageFiles.Length(); ++i)
		if (m_imageFiles[i] == filename)
			return i;
	m_imageFiles.Append(filename);
	AutoArray<uint8_t>* image = m_data.images.Append();
	m_data.imageNames.Append(String(filename.generic_string().c_str()));
	std::ifstream file(filename, std::ios::binary | std::ios::ate);
	if (not file) {
		logHandler.Print("ObjLoader: cannot open texture '%s'\n", filename.string().c_str());
		missingFiles.Report(filename.string().c_str());
		return m_imageFiles.Length() - 1;
	}
	std::streamoff size = file.tellg();
	file.seekg(0);
	image->Resize(int32_t(size));
	if ((size > 0) and not file.read(reinterpret_cast<char*>(image->DataPtr()), size)) {
		logHandler.Print("ObjLoader: cannot read texture '%s'\n", filename.string().c_str());
		image->Clear();
	}
	return m_imageFiles.Length() - 1;
}

// =================================================================================================
