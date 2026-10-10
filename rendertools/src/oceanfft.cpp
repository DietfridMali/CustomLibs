#include <cmath>
#include <limits>
#include <new>
#include <random>

#include "gfxrenderer.h"
#include "gfxstates.h"
#include "base_shaderhandler.h"
#include "compute_shader.h"
#include "gfxarray.hpp"
#include "oceanfft.h"
#include "loghandler.h"

// =================================================================================================

static constexpr float cTwoPi = 6.28318530718f;

static float WaveEnergy(const OceanFFT::Wave& wave, float gravity, float kx, float kz, float kLen)
{
	float peakOmega = sqrtf(gravity * cTwoPi / wave.length);
	float omega = sqrtf(gravity * kLen);
	float sigma = (omega <= peakOmega) ? 0.07f : 0.09f;
	float offset = (omega - peakOmega) / (sigma * peakOmega);
	float ratio = peakOmega / omega;
	float shape = gravity * gravity / powf(omega, 5.0f) * expf(-1.25f * powf(ratio, 4.0f)) *
		powf(wave.peakedness, expf(-0.5f * offset * offset));
	float directional = powf(fabsf(cosf(0.5f * (atan2f(kz, kx) - wave.heading))), 2.0f * wave.spread);
	return shape * (0.5f * gravity / omega) / kLen * directional;
}


RenderTarget* OceanFFT::CreateTarget(const char* name, int width, int height, int bufferCount, GfxPixelFormat format)
{
	RenderTarget* target = new (std::nothrow) RenderTarget();
	if (not target)
		return nullptr;
	RenderTarget::RTCreationParams params;
	params.name = name;
	params.colorBufferCount = 0;
	params.skyMapCount = bufferCount;
	params.skyMapFormat = ToNativeColorFormat(format);
	if (not target->Create(width, height, 1, params)) {
		delete target;
		return nullptr;
	}
	gfxStates.ClearSkyMaps(target);
	return target;
}


bool OceanFFT::Create(const Params& params)
{
	Destroy();
	m_params = params;
	m_work = CreateTarget("oceanWork", cGridSize * cCascadeCount, cGridSize, cWorkBufferCount, GfxPixelFormat::RGBA32_SFloat);
	bool ok = (m_work != nullptr);
	for (int c = 0; ok and (c < cCascadeCount); ++c) {
		m_displacement[c][0] = CreateTarget("oceanDisplacement", cGridSize, cGridSize, 1, GfxPixelFormat::RGBA16_SFloat);
		m_displacement[c][1] = CreateTarget("oceanDisplacement", cGridSize, cGridSize, 1, GfxPixelFormat::RGBA16_SFloat);
		m_derivatives[c] = CreateTarget("oceanDerivatives", cGridSize, cGridSize, 1, GfxPixelFormat::RGBA16_SFloat);
		ok = (m_displacement[c][0] != nullptr) and (m_displacement[c][1] != nullptr) and (m_derivatives[c] != nullptr);
	}
	if (not ok) {
		logHandler.Print("OceanFFT: creating the wave targets failed\n");
		Destroy();
		return false;
	}
	m_isAvailable = true;
	return true;
}


void OceanFFT::Destroy(void)
{
	delete m_work;
	m_work = nullptr;
	for (int c = 0; c < cCascadeCount; ++c) {
		delete m_displacement[c][0];
		delete m_displacement[c][1];
		delete m_derivatives[c];
		m_displacement[c][0] = nullptr;
		m_displacement[c][1] = nullptr;
		m_derivatives[c] = nullptr;
	}
	m_current = 0;
	m_isAvailable = false;
	m_hasSpectrum = false;
	m_isReady = false;
}


void OceanFFT::BuildSpectrum(Vector4f* spectrum) const
{
	const int size = cGridSize;
	const int cells = size * size;

	AutoArray<float> swellEnergy;
	swellEnergy.Resize(cells * cCascadeCount);
	AutoArray<float> windEnergy;
	windEnergy.Resize(cells * cCascadeCount);
	double swellSum = 0.0;
	double windSum = 0.0;

	for (int c = 0; c < cCascadeCount; ++c) {
		float dk = cTwoPi / m_params.tileSizes[c];
		float kMin = (c > 0) ? cBandFactor * dk : 0.0f;
		float kMax = (c + 1 < cCascadeCount) ? cBandFactor * cTwoPi / m_params.tileSizes[c + 1] : std::numeric_limits<float>::max();
		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				int i = c * cells + y * size + x;
				swellEnergy[i] = 0.0f;
				windEnergy[i] = 0.0f;
				if ((x == size / 2) or (y == size / 2))
					continue;
				float kx = float((x < size / 2) ? x : x - size) * dk;
				float kz = float((y < size / 2) ? y : y - size) * dk;
				float kLen = sqrtf(kx * kx + kz * kz);
				if ((kLen <= 0.0f) or (kLen < kMin) or (kLen >= kMax))
					continue;
				float cutoff = kLen * m_params.shortestWave / cTwoPi;
				float weight = expf(-cutoff * cutoff) * dk * dk;
				swellEnergy[i] = WaveEnergy(m_params.swell, m_params.gravity, kx, kz, kLen) * weight;
				windEnergy[i] = WaveEnergy(m_params.windSea, m_params.gravity, kx, kz, kLen) * weight;
				swellSum += double(swellEnergy[i]);
				windSum += double(windEnergy[i]);
			}
		}
	}

	float swellSigma = 0.25f * m_params.swell.height;
	float windSigma = 0.25f * m_params.windSea.height;
	float swellScale = (swellSum > 0.0) ? float(double(swellSigma * swellSigma) / (2.0 * swellSum)) : 0.0f;
	float windScale = (windSum > 0.0) ? float(double(windSigma * windSigma) / (2.0 * windSum)) : 0.0f;

	AutoArray<Vector2f> amplitudes;
	amplitudes.Resize(cells * cCascadeCount);
	std::mt19937					generator(m_params.seed);
	std::normal_distribution<float>	gauss(0.0f, 1.0f);
	for (int i = 0; i < cells * cCascadeCount; ++i) {
		float amplitude = sqrtf(0.5f * (swellScale * swellEnergy[i] + windScale * windEnergy[i]));
		float re = gauss(generator) * amplitude;
		float im = gauss(generator) * amplitude;
		amplitudes[i] = Vector2f(re, im);
	}

	for (int c = 0; c < cCascadeCount; ++c) {
		for (int y = 0; y < size; ++y) {
			for (int x = 0; x < size; ++x) {
				const Vector2f& h0 = amplitudes[c * cells + y * size + x];
				const Vector2f& mirrored = amplitudes[c * cells + ((size - y) % size) * size + ((size - x) % size)];
				spectrum[y * size * cCascadeCount + c * size + x] = Vector4f(h0.X(), h0.Y(), mirrored.X(), mirrored.Y());
			}
		}
	}
}


void OceanFFT::SetConstants(ComputeShader* shader, float time, float deltaTime) const
{
	shader->SetVector4f("tileSizes", Vector4f(m_params.tileSizes[0], m_params.tileSizes[1], m_params.tileSizes[2], 0.0f));
	shader->SetFloat("time", time);
	shader->SetFloat("deltaTime", deltaTime);
	shader->SetFloat("gravity", m_params.gravity);
	shader->SetFloat("choppiness", m_params.choppiness);
	shader->SetFloat("foamDecay", m_params.foamDecay);
	shader->SetInt("gridSize", cGridSize);
	shader->SetInt("logSize", cLogSize);
	shader->SetInt("stage", 0);
	shader->SetInt("isVertical", 0);
	shader->SetInt("cascade", 0);
}


bool OceanFFT::UploadSpectrum(void)
{
	ComputeShader* shader = baseShaderHandler.SetupComputeShader("oceanInit");
	if ((shader == nullptr) or not shader->IsValid())
		return false;

	GfxArray<Vector4f, GfxTypes::StructuredBuffer> spectrum;
	if (not spectrum.Create(cGridSize * cCascadeCount * cGridSize))
		return false;
	BuildSpectrum(spectrum.Data());
	if (not spectrum.UploadImmediate())
		return false;

	bool ok = spectrum.Bind(1);
	if (ok) {
		SetConstants(shader, 0.0f, 0.0f);
		shader->BindStorageImage(36, m_work, m_work->m_computeBufferIndex);
		ok = shader->Dispatch((uint32_t(cGridSize * cCascadeCount) + cTileSize - 1u) / cTileSize,
							  (uint32_t(cGridSize) + cTileSize - 1u) / cTileSize, 1);
	}
	spectrum.Release(1);
	return ok;
}


bool OceanFFT::Update(float time, float deltaTime)
{
	if (not m_isAvailable)
		return false;
	void* operation = baseRenderer.StartOperation("OceanFFT::Update", false);
	if (operation == nullptr)
		return false;
	bool ok = Compute(time, deltaTime);
	baseRenderer.FinishOperation(operation);
	return ok;
}


bool OceanFFT::Compute(float time, float deltaTime)
{
	if (not m_hasSpectrum) {
		if (not UploadSpectrum())
			return false;
		m_hasSpectrum = true;
	}

	ComputeShader* spectrum = baseShaderHandler.SetupComputeShader("oceanSpectrum");
	ComputeShader* transform = baseShaderHandler.SetupComputeShader("oceanTransform");
	ComputeShader* assemble = baseShaderHandler.SetupComputeShader("oceanAssemble");
	if ((spectrum == nullptr) or (transform == nullptr) or (assemble == nullptr))
		return false;
	if (not (spectrum->IsValid() and transform->IsValid() and assemble->IsValid()))
		return false;

	const int		base = m_work->m_computeBufferIndex;
	const uint32_t	atlasGroups = (uint32_t(cGridSize * cCascadeCount) + cTileSize - 1u) / cTileSize;
	const uint32_t	gridGroups = (uint32_t(cGridSize) + cTileSize - 1u) / cTileSize;

	SetConstants(spectrum, time, deltaTime);
	spectrum->BindSampledImage(4, m_work, base);
	spectrum->BindStorageImage(36, m_work, base + 1);
	spectrum->BindStorageImage(37, m_work, base + 2);
	if (not spectrum->Dispatch(atlasGroups, gridGroups, 1))
		return false;

	int source = 1;
	for (int pass = 0; pass < 2 * cLogSize; ++pass) {
		int target = (source == 1) ? 3 : 1;
		SetConstants(transform, time, deltaTime);
		transform->SetInt("stage", pass % cLogSize);
		transform->SetInt("isVertical", (pass < cLogSize) ? 0 : 1);
		transform->BindSampledImage(4, m_work, base + source);
		transform->BindSampledImage(5, m_work, base + source + 1);
		transform->BindStorageImage(36, m_work, base + target);
		transform->BindStorageImage(37, m_work, base + target + 1);
		if (not transform->Dispatch(atlasGroups, gridGroups, 1))
			return false;
		source = target;
	}

	int previous = m_current;
	int current = 1 - m_current;
	for (int c = 0; c < cCascadeCount; ++c) {
		SetConstants(assemble, time, deltaTime);
		assemble->SetInt("cascade", c);
		assemble->BindSampledImage(4, m_work, base + source);
		assemble->BindSampledImage(5, m_work, base + source + 1);
		assemble->BindSampledImage(6, m_displacement[c][previous], m_displacement[c][previous]->m_computeBufferIndex);
		assemble->BindStorageImage(36, m_displacement[c][current], m_displacement[c][current]->m_computeBufferIndex);
		assemble->BindStorageImage(37, m_derivatives[c], m_derivatives[c]->m_computeBufferIndex);
		if (not assemble->Dispatch(gridGroups, gridGroups, 1))
			return false;
	}
	m_current = current;
	m_isReady = true;
	return true;
}


Texture* OceanFFT::MapTexture(RenderTarget* target)
{
	if (not target)
		return nullptr;
	Texture* texture = target->GetAsTexture({ .source = target->m_computeBufferIndex });
	if (texture)
		texture->m_sampling = TextureSampling{};
	return texture;
}


Texture* OceanFFT::DisplacementMap(int cascade)
{
	return MapTexture(m_displacement[cascade][m_current]);
}


Texture* OceanFFT::DerivativeMap(int cascade)
{
	return MapTexture(m_derivatives[cascade]);
}

// =================================================================================================
