
#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"
#include "compute_shader.h"
#include "loghandler.h"

// =================================================================================================

const ShaderSource& TestShader();
const ShaderSource& StencilShader();
const ShaderSource& SurfaceShadowShader();
const ShaderSource& SphereShadowShader();
const ShaderSource& DepthRenderer();
const ShaderSource& LineShader();
const ShaderSource& RingShader();
const ShaderSource& CircleShader();
const ShaderSource& CircleMaskShader();
const ShaderSource& RectangleShader();
const ShaderSource& ShadedRectangleShader();
const ShaderSource& ShadedRingShader();
const ShaderSource& ColorMeshShader();
const ShaderSource& PlainColorShader();
const ShaderSource& PlainTextureShader();
const ShaderSource& PlainScreenTextureShader();
const ShaderSource& ColoredTextureShader();
const ShaderSource& GlyphShader();
const ShaderSource& MovingTextureShader();
const ShaderSource& BlurTextureShader();
const ShaderSource& GrayScaleShader();
const ShaderSource& TintAndBlurShader();
const ShaderSource& OutlineShader();
const ShaderSource& BevelShader();
const ShaderSource& BoxBlurShader();
const ShaderSource& ToneMapShader();
const ShaderSource& FxaaShader();
const ShaderSource& GaussBlurShader();
const ShaderSource& BilateralBlurShader();
const ShaderSource& BilateralBlurDepthShader();
const ShaderSource& LightningDrawShader();
const ShaderSource& LightningFlareShader();
const ShaderSource& LineDrawShader();
const ShaderSource& SkyboxShader();
const ShaderSource& BlackholeShader();
const ShaderSource& KuwaharaTensorShader();
const ShaderSource& KuwaharaScreenTensorShader();
const ShaderSource& KuwaharaScreenFilterShader();
const ShaderSource& KuwaharaTensorBlurShader();
const ShaderSource& KuwaharaFilterShader();
const ShaderSource& KuwaharaCubeTensorShader();
const ShaderSource& KuwaharaCubeFilterShader();
const ShaderSource& KuwaharaSourceShader();
const ShaderSource& KuwaharaCubeSourceShader();
const ShaderSource& KuwaharaGaussShader();
const ShaderSource& OceanInitShader();
const ShaderSource& OceanSpectrumShader();
const ShaderSource& OceanTransformShader();
const ShaderSource& OceanAssembleShader();
const ShaderSource& XRLayerCopyShader();
const ShaderSource& XRLayerMirrorShader();
const ShaderSource& XRMirrorShader();
const ShaderSource& XRDepthCopyShader();

// -------------------------------------------------------------------------------------------------

BaseShaderCode::BaseShaderCode(const String& shaderFolder)
	: m_shaderFolder(shaderFolder)
{
	AutoArray<const ShaderSource*> shaderSource = {
		&TestShader(),
		&StencilShader(),
		&SurfaceShadowShader(),
		&SphereShadowShader(),
		&DepthRenderer(),
		&LineShader(),
		&RingShader(),
		&CircleShader(),
		&CircleMaskShader(),
		&RectangleShader(),
		&ShadedRectangleShader(),
		&ShadedRingShader(),
		&ColorMeshShader(),
		&PlainColorShader(),
		&PlainTextureShader(),
		&PlainScreenTextureShader(),
		&ColoredTextureShader(),
		&GlyphShader(),
		&MovingTextureShader(),
		&BlurTextureShader(),
		&GrayScaleShader(),
		&TintAndBlurShader(),
		&OutlineShader(),
		&BevelShader(),
		&BoxBlurShader(),
		&ToneMapShader(),
		&FxaaShader(),
		&GaussBlurShader(),
		&BilateralBlurShader(),
		&BilateralBlurDepthShader(),
		&LightningDrawShader(),
		&LightningFlareShader(),
		&LineDrawShader(),
		&SkyboxShader(),
		&BlackholeShader(),
		&KuwaharaTensorShader(),
		&KuwaharaScreenTensorShader(),
		&KuwaharaScreenFilterShader(),
		&KuwaharaTensorBlurShader(),
		&KuwaharaFilterShader(),
		&KuwaharaCubeTensorShader(),
		&KuwaharaCubeFilterShader(),
		&KuwaharaSourceShader(),
		&KuwaharaCubeSourceShader(),
		&KuwaharaGaussShader(),
		&OceanInitShader(),
		&OceanSpectrumShader(),
		&OceanTransformShader(),
		&OceanAssembleShader(),
		&XRLayerCopyShader(),
		&XRLayerMirrorShader(),
		&XRMirrorShader(),
		&XRDepthCopyShader()
	};
	AddShaders(shaderSource);
}


static String FormatCompilerArgs(const AutoArray<ShaderMacro>& macros)
{
	String out;
	for (const ShaderMacro& m : macros)
		out = out + String("#define ") + m.m_name + String(" ") + m.m_value + String("\n");
	return out;
}


void BaseShaderCode::AddShaders(AutoArray<const ShaderSource*>& shaderSource)
{
	for (const ShaderSource* source : shaderSource)
		m_shaderSources.Append(source);
}


void BaseShaderCode::CreateShaders(void)
{
	for (const ShaderSource* source : m_shaderSources) {
		if (source)
			CreateShader(source);
	}
	m_shaderSources.Clear();
}


void BaseShaderCode::CreateShaders(const AutoArray<String>& shaderIds)
{
	for (const ShaderSource*& source : m_shaderSources) {
		if (not source)
			continue;
		for (const String& shaderId : shaderIds) {
			if (source->m_name == shaderId) {
				CreateShader(source);
				source = nullptr;
				break;
			}
		}
	}
}


void BaseShaderCode::RecreateShaders(void)
{
	for (auto& entry : m_shaders) {
		if (entry.second and not entry.second->Recreate(m_shaderFolder))
			logHandler.Print("recreating shader '%s' failed\n", static_cast<const char*>(entry.first));
	}
	for (auto& entry : m_computeShaders) {
		if (entry.second and not entry.second->Recreate(m_shaderFolder))
			logHandler.Print("recreating compute shader '%s' failed\n", static_cast<const char*>(entry.first));
	}
}


bool BaseShaderCode::RecreateShader(const String& shaderId)
{
	Shader** shader = m_shaders.Find(shaderId);

	if (not (shader and *shader))
		return false;
	if ((*shader)->Recreate(m_shaderFolder))
		return true;
	logHandler.Print("recreating shader '%s' failed\n", static_cast<const char*>(shaderId));
	return false;
}


bool BaseShaderCode::RecreateShader(int index)
{
	for (auto& entry : m_shaders) {
		if (index-- > 0)
			continue;
		if (entry.second and not entry.second->Recreate(m_shaderFolder)) {
			logHandler.Print("recreating shader '%s' failed\n", static_cast<const char*>(entry.first));
			return false;
		}
		return true;
	}
	for (auto& entry : m_computeShaders) {
		if (index-- > 0)
			continue;
		if (entry.second and not entry.second->Recreate(m_shaderFolder)) {
			logHandler.Print("recreating compute shader '%s' failed\n", static_cast<const char*>(entry.first));
			return false;
		}
		return true;
	}
	return false;
}


void BaseShaderCode::DestroyShaders(void)
{
	for (auto& entry : m_shaders) {
		if (entry.second)
			entry.second->Destroy();
	}
	for (auto& entry : m_computeShaders) {
		if (entry.second)
			entry.second->Destroy();
	}
}


void BaseShaderCode::CreateShader(const ShaderSource* source)
{
	String prefix = FormatCompilerArgs(source->m_compilerArgs);
	if (source->IsCompute()) {
		String			cs = prefix + source->m_cs;
		ComputeShader*	shader = new ComputeShader(source->m_name);
		shader->m_cs = cs;
		if (shader->Create(cs, source->m_computeBindings, m_shaderFolder)) {
			m_computeShaders[source->m_name] = shader;
		}
		else {
#ifdef _DEBUG
			logHandler.Print("creating compute shader '%s' failed\n", (const char*)source->m_name);
#endif
			delete shader;
		}
		return;
	}
	String	vs = prefix + source->m_vs;
	String	fs = prefix + source->m_fs;
	String	gs = source->m_gs.IsEmpty() ? String() : (prefix + source->m_gs);
	String	tcs = source->m_tcs.IsEmpty() ? String() : (prefix + source->m_tcs);
	String	tes = source->m_tes.IsEmpty() ? String() : (prefix + source->m_tes);
	Shader*	shader = new Shader(source->m_name, vs, fs, gs);
	shader->m_dataLayout = source->m_dataLayout;
	if (shader->Create(vs, fs, gs, tcs, tes, m_shaderFolder))
		m_shaders[source->m_name] = shader;
	else {
#ifdef _DEBUG
		logHandler.Print("creating shader '%s' failed\n", (const char*)source->m_name);
#endif
		delete shader;
	}
}

// =================================================================================================
