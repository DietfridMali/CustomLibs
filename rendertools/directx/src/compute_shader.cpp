#define NOMINMAX

#include <utility>
#include <cstring>
#include <limits>
#include <vector>
#include <string>

#include "dx12framework.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxc/dxcapi.h>
#include <wrl/client.h>

#include "compute_shader.h"
#include "shader.h"
#include "shadercache.h"
#include "renderstates.h"
#include "dx12context.h"
#include "cbv_allocator.h"
#include "commandlist.h"
#include "descriptor_heap.h"
#include "rendertarget.h"
#include "gfxrenderer.h"
#include "loghandler.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")

using Microsoft::WRL::ComPtr;

// =================================================================================================
// DX12 ComputeShader implementation
//
// HLSL CS source -> DXIL via DXC (target cs_6_0). Root signature is built from the binding
// descriptor list: one root CBV per UniformBuffer entry, one descriptor table per SRV/Sampler/
// UAV slot (1-entry table each, mirroring the graphics Shader's layout). Pipeline created via
// CreateComputePipelineState.
//
// The runtime calls (SetComputeRootSignature / SetPipelineState / Dispatch / table binds)
// happen in the caller — see directx/src/cloudrenderer.cpp for the TSP driver.
// =================================================================================================

namespace {

ComPtr<IDxcUtils>		g_dxcUtils;
ComPtr<IDxcCompiler3>	g_dxcCompiler;
bool					g_dxcInitialized = false;

bool InitDxc(void)
noexcept
{
	if (g_dxcInitialized)
		return true;
	if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(g_dxcUtils.GetAddressOf()))))
		return false;
	if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(g_dxcCompiler.GetAddressOf())))) {
		g_dxcUtils.Reset();
		return false;
	}
	g_dxcInitialized = true;
	return true;
}

std::wstring ToWide(const char* utf8)
noexcept
{
	std::wstring s;
	if (utf8)
		while (*utf8)
			s.push_back(wchar_t(uint8_t(*utf8++)));
	return s;
}

static_assert(ComputeShader::kAccelSpace == uint32_t(Shader::kAccelSpace),
			  "compute and graphics shaders share the HLSL declaration of the acceleration structure");
static_assert(ComputeShader::kReadOnlySlots == uint32_t(Shader::kSsboSlots),
			  "compute and graphics shaders share the HLSL declaration of the read only buffers");
static_assert(ComputeShader::kReadOnlySpace == uint32_t(Shader::kSsboSpace),
			  "compute and graphics shaders share the HLSL declaration of the read only buffers");
static_assert(ComputeShader::kReadOnlySlots == CommandList::kSsboSlots,
			  "ComputeShader and CommandList must agree on the read only buffer slot count");

constexpr const char* kAccelTypeName = "RaytracingAccelerationStructure";

} // namespace

#ifdef _DEBUG
static constexpr const char*	kOptimizationLevel = "-Od";
static constexpr const wchar_t*	kOptimizationArg = L"-Od";
#else
static constexpr const char*	kOptimizationLevel = "-O3";
static constexpr const wchar_t*	kOptimizationArg = L"-O3";
#endif


bool ComputeShader::Compile(const char* hlslCode, const char* entryPoint, const String& shaderFolder)
{
	if ((not hlslCode) or (not *hlslCode))
		return false;
	if (not InitDxc()) {
		logHandler.Print("ComputeShader '%s': DXC initialization failed\n", (const char*)m_name);
		return false;
	}

	DxcBuffer src{};
	src.Ptr = hlslCode;
	src.Size = std::strlen(hlslCode);
	src.Encoding = DXC_CP_ACP;

	const char* target = "cs_6_0";
	if (std::strstr(hlslCode, kAccelTypeName) != nullptr)
		target = "cs_6_5";

	std::wstring			wEntry = ToWide(entryPoint);
	std::wstring			wTarget = ToWide(target);
	std::vector<LPCWSTR>	args = {
		L"-E",
		wEntry.c_str(),
		L"-T",
		wTarget.c_str(),
		L"-Zpc", // column-major matrices
		L"-Wno-ignored-attributes",
#ifdef _DEBUG
		L"-Zi",
#endif
		kOptimizationArg,
	};

	const bool		useCache = not shaderFolder.IsEmpty();
	const String	fileName = m_name + String(".") + String(target) + String(kOptimizationLevel) + String(".dxil");
	uint64_t		key = 0;
	if (useCache) {
		key = ShaderCache::Hash(ShaderCache::kHashSeed, hlslCode);
		key = ShaderCache::Hash(key, args.data(), args.size());
		ComPtr<IDxcVersionInfo> versionInfo;
		if (SUCCEEDED(g_dxcCompiler->QueryInterface(IID_PPV_ARGS(versionInfo.GetAddressOf())))) {
			UINT32 major = 0;
			UINT32 minor = 0;
			versionInfo->GetVersion(&major, &minor);
			key = ShaderCache::Hash(key, &major, sizeof(major));
			key = ShaderCache::Hash(key, &minor, sizeof(minor));
		}
		std::vector<uint8_t>	dxil;
		uint32_t				tag = 0;
		if (ShaderCache::Read(shaderFolder, fileName, key, dxil, tag) and
			SUCCEEDED(D3DCreateBlob(dxil.size(), m_csBytecode.ReleaseAndGetAddressOf()))) {
			std::memcpy(m_csBytecode->GetBufferPointer(), dxil.data(), dxil.size());
			return true;
		}
	}

	ComPtr<IDxcResult>	result;
	HRESULT				hr = g_dxcCompiler->Compile(&src, args.data(), UINT32(args.size()), nullptr, IID_PPV_ARGS(result.GetAddressOf()));
	if (FAILED(hr)) {
		logHandler.Print("ComputeShader '%s' (%s): DXC Compile call failed (0x%08X)\n", (const char*)m_name, target, (unsigned)hr);
		return false;
	}

	ComPtr<IDxcBlobUtf8> errors;
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errors.GetAddressOf()), nullptr);
	const bool hasOutput = errors and (errors->GetStringLength() > 0);
#ifdef _DEBUG
	if (hasOutput)
		logHandler.Print("ComputeShader '%s': %s\n", (const char*)m_name, errors->GetStringPointer());
#endif

	HRESULT status = E_FAIL;
	result->GetStatus(&status);
	if (FAILED(status)) {
		logHandler.Print("ComputeShader '%s' (%s): compile failed - %s\n", (const char*)m_name, target,
						 static_cast<const char*>(ShaderErrorSummary(hasOutput ? errors->GetStringPointer() : nullptr)));
		return false;
	}

	ComPtr<IDxcBlob> obj;
	if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(obj.GetAddressOf()), nullptr)) or not obj) {
		logHandler.Print("ComputeShader '%s' (%s): no DXIL output\n", (const char*)m_name, target);
		return false;
	}

	if (FAILED(D3DCreateBlob(obj->GetBufferSize(), m_csBytecode.GetAddressOf()))) {
		logHandler.Print("ComputeShader '%s' (%s): no memory for the compiled code\n", (const char*)m_name, target);
		return false;
	}
	std::memcpy(m_csBytecode->GetBufferPointer(), obj->GetBufferPointer(), obj->GetBufferSize());
	if (useCache)
		ShaderCache::Write(shaderFolder, fileName, key, 0, static_cast<const uint8_t*>(m_csBytecode->GetBufferPointer()),
						   m_csBytecode->GetBufferSize());
	return true;
}


bool ComputeShader::CreateRootSignature(const AutoArray<ComputeBindingDesc>& bindings)
noexcept
{
	ID3D12Device* device = dx12Context.Device();
	if (not device)
		return false;

	// Pass 1: bucketize bindings by kind. SPIR-V binding numbers map to HLSL registers:
	//   binding 0 → b0      binding 1 → b1
	//   binding 4..19 → t0..t15
	//   binding 20..35 → s0..s15
	//   binding 36..39 → u0..u3
	std::vector<D3D12_DESCRIPTOR_RANGE>							srvRanges;
	std::vector<D3D12_DESCRIPTOR_RANGE>							samplerRanges;
	std::vector<D3D12_DESCRIPTOR_RANGE>							uavRanges;
	std::vector<std::pair<uint32_t, ComputeBindingDesc::Kind>>	orderedBindings; // root-param order

	auto registerForBinding = [](uint32_t binding, ComputeBindingDesc::Kind kind) -> uint32_t {
		switch (kind) {
			case ComputeBindingDesc::Kind::UniformBuffer:
				return binding; // b0/b1
			case ComputeBindingDesc::Kind::SampledImage:
				return binding - 4u; // t0..
			case ComputeBindingDesc::Kind::Sampler:
				return binding - 20u; // s0..
			case ComputeBindingDesc::Kind::StorageImage:
				return binding - 36u; // u0..
			case ComputeBindingDesc::Kind::StorageBuffer:
				return binding - 36u; // u0..
			case ComputeBindingDesc::Kind::ReadOnlyBuffer:
				return binding - kReadOnlyBase;
			default:
				return binding;
		}
	};

	for (int i = 0; i < bindings.Length(); ++i)
		orderedBindings.push_back({ bindings[i].binding, bindings[i].kind });

	// Allocate root parameters in the order of bindings — caller side knows the order and
	// SetComputeRootDescriptorTable(rootIdx, ...) uses these slots.
	std::vector<D3D12_ROOT_PARAMETER> params;
	params.reserve(orderedBindings.size());

	// Reserve room so the D3D12_DESCRIPTOR_RANGE pointers stay stable.
	srvRanges.reserve(16);
	samplerRanges.reserve(16);
	uavRanges.reserve(4);
	std::vector<D3D12_DESCRIPTOR_RANGE> readOnlyRanges;
	readOnlyRanges.reserve(kReadOnlySlots);

	for (size_t i = 0; i < orderedBindings.size(); ++i) {
		uint32_t					binding = orderedBindings[i].first;
		ComputeBindingDesc::Kind	kind = orderedBindings[i].second;
		uint32_t					reg = registerForBinding(binding, kind);

		D3D12_ROOT_PARAMETER p{};
		p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		if (kind == ComputeBindingDesc::Kind::UniformBuffer) {
			p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			p.Descriptor.ShaderRegister = reg;
			p.Descriptor.RegisterSpace = 0;
			if (reg < 2)
				m_cbvRootIndex[reg] = int32_t(params.size());
		}
		else {
			D3D12_DESCRIPTOR_RANGE r{};
			r.NumDescriptors = 1;
			r.BaseShaderRegister = reg;
			r.RegisterSpace = 0;
			r.OffsetInDescriptorsFromTableStart = 0;
			if (kind == ComputeBindingDesc::Kind::SampledImage) {
				r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
				srvRanges.push_back(r);
				p.DescriptorTable.NumDescriptorRanges = 1;
				p.DescriptorTable.pDescriptorRanges = &srvRanges.back();
				if (reg < 16)
					m_srvRootIndex[reg] = int32_t(params.size());
			}
			else if (kind == ComputeBindingDesc::Kind::Sampler) {
				r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
				samplerRanges.push_back(r);
				p.DescriptorTable.NumDescriptorRanges = 1;
				p.DescriptorTable.pDescriptorRanges = &samplerRanges.back();
				if (reg < 16)
					m_samplerRootIndex[reg] = int32_t(params.size());
			}
			else if ((kind == ComputeBindingDesc::Kind::StorageImage) or (kind == ComputeBindingDesc::Kind::StorageBuffer)) {
				r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
				uavRanges.push_back(r);
				p.DescriptorTable.NumDescriptorRanges = 1;
				p.DescriptorTable.pDescriptorRanges = &uavRanges.back();
				if (reg < 4)
					m_uavRootIndex[reg] = int32_t(params.size());
			}
			else if (kind == ComputeBindingDesc::Kind::ReadOnlyBuffer) {
				if (reg >= kReadOnlySlots)
					continue;
				r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
				r.RegisterSpace = kReadOnlySpace;
				readOnlyRanges.push_back(r);
				p.DescriptorTable.NumDescriptorRanges = 1;
				p.DescriptorTable.pDescriptorRanges = &readOnlyRanges.back();
				m_readOnlyRootIndex[reg] = int32_t(params.size());
			}
			else {
				continue; // unsupported kind
			}
			p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		}
		params.push_back(p);
	}

	if (m_usesAccelStructure) {
		D3D12_ROOT_PARAMETER p{};
		p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		p.Descriptor.ShaderRegister = 0;
		p.Descriptor.RegisterSpace = kAccelSpace;
		p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		m_accelRootIndex = int32_t(params.size());
		params.push_back(p);
	}

	D3D12_ROOT_SIGNATURE_DESC rsd{};
	rsd.NumParameters = UINT(params.size());
	rsd.pParameters = params.data();
	rsd.NumStaticSamplers = 0;
	rsd.pStaticSamplers = nullptr;
	rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ComPtr<ID3DBlob>	sig, err;
	HRESULT				hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, sig.GetAddressOf(), err.GetAddressOf());
	if (FAILED(hr)) {
		logHandler.Print("ComputeShader '%s': root signature could not be serialized - %s\n", (const char*)m_name,
						 static_cast<const char*>(ShaderErrorSummary(err ? static_cast<const char*>(err->GetBufferPointer()) : nullptr)));
		return false;
	}
	m_rootSignatureBlob = sig;
	if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
										   IID_PPV_ARGS(m_rootSignature.GetAddressOf())))) {
		logHandler.Print("ComputeShader '%s': root signature could not be created\n", (const char*)m_name);
		return false;
	}
	return true;
}


bool ComputeShader::CreatePipeline(void)
noexcept
{
	ID3D12Device* device = dx12Context.Device();
	if (not device or not m_rootSignature or not m_csBytecode)
		return false;

	D3D12_COMPUTE_PIPELINE_STATE_DESC psd{};
	psd.pRootSignature = m_rootSignature.Get();
	psd.CS.pShaderBytecode = m_csBytecode->GetBufferPointer();
	psd.CS.BytecodeLength = m_csBytecode->GetBufferSize();
	psd.NodeMask = 0;
	psd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	if (not PSO::CreateComputePipeline(device, psd, m_name, m_rootSignatureBlob.Get(), m_pipeline)) {
		logHandler.Print("ComputeShader '%s': pipeline could not be created\n", (const char*)m_name);
		return false;
	}
	return true;
}


bool ComputeShader::Create(const String& csCode, const AutoArray<ComputeBindingDesc>& bindings, const String& shaderFolder)
{
	if (IsValid())
		return true;
	m_usesAccelStructure = std::strstr(static_cast<const char*>(csCode), kAccelTypeName) != nullptr;
	if (m_usesAccelStructure and not dx12Context.HasRayTracing()) {
#ifdef _DEBUG
		logHandler.Print("ComputeShader '%s': needs ray tracing, which this device does not have - not created\n", (const char*)m_name);
#endif
		m_usesAccelStructure = false;
		return false;
	}
	if (not Compile((const char*)csCode, "CSMain", shaderFolder))
		return false;
	m_bindings = bindings;
	if (not CreateRootSignature(bindings))
		return false;
	if (not CreatePipeline())
		return false;
	ReflectB1Fields();
	if (m_b1Size > 0) {
		m_b1Staging.assign(m_b1Size, 0);
		m_b1Dirty = true;
	}
	m_cs = csCode;
	return true;
}


void ComputeShader::ReflectB1Fields(void)
noexcept
{
	if (not m_csBytecode or not InitDxc())
		return;
	ComPtr<ID3D12ShaderReflection> refl;
	{
		DxcBuffer rb{};
		rb.Ptr = m_csBytecode->GetBufferPointer();
		rb.Size = m_csBytecode->GetBufferSize();
		rb.Encoding = DXC_CP_ACP;
		if (FAILED(g_dxcUtils->CreateReflection(&rb, IID_PPV_ARGS(refl.GetAddressOf()))))
			return;
	}

	D3D12_SHADER_DESC sd{};
	refl->GetDesc(&sd);
	for (UINT i = 0; i < sd.ConstantBuffers; ++i) {
		ID3D12ShaderReflectionConstantBuffer*	cb = refl->GetConstantBufferByIndex(i);
		D3D12_SHADER_BUFFER_DESC				cbd{};
		cb->GetDesc(&cbd);
		if (strcmp(cbd.Name, "ShaderConstants") != 0)
			continue;
		if (cbd.Size > m_b1Size)
			m_b1Size = cbd.Size;
		for (UINT j = 0; j < cbd.Variables; ++j) {
			ID3D12ShaderReflectionVariable*	var = cb->GetVariableByIndex(j);
			D3D12_SHADER_VARIABLE_DESC		vd{};
			var->GetDesc(&vd);
			bool found = false;
			for (auto& kv : m_b1Fields)
				if (kv.first == String(vd.Name)) {
					found = true;
					break;
				}
			if (not found) {
				auto* entry = m_b1Fields.Append();
				if (entry)
					*entry = { String(vd.Name), { vd.StartOffset, vd.Size } };
			}
		}
		break;
	}
}


void ComputeShader::Destroy(void)
noexcept
{
	m_pipeline.Reset();
	m_rootSignature.Reset();
	m_rootSignatureBlob.Reset();
	m_csBytecode.Reset();
	m_b1Staging.clear();
	m_b1Fields.Reset();
	m_b1Size = 0;
	m_b1Dirty = true;
	m_b1GpuVA = 0;
	for (int i = 0; i < 2; ++i)
		m_cbvRootIndex[i] = -1;
	for (int i = 0; i < 16; ++i) {
		m_srvRootIndex[i] = -1;
		m_samplerRootIndex[i] = -1;
	}
	for (int i = 0; i < 4; ++i)
		m_uavRootIndex[i] = -1;
	for (uint32_t i = 0; i < kReadOnlySlots; ++i)
		m_readOnlyRootIndex[i] = -1;
	m_accelRootIndex = -1;
	m_usesAccelStructure = false;
}


bool ComputeShader::Activate(void)
{
	return IsValid();
}


void ComputeShader::ResetImageBindings(void)
noexcept
{
	for (ImageBinding& image : m_sampledImages)
		image = ImageBinding{};
	for (ImageBinding& image : m_storageImages)
		image = ImageBinding{};
}


bool ComputeShader::BindSampledImage(uint32_t binding, RenderTarget* target, int bufferIndex)
{
	if ((binding < kSampledBase) or (binding >= kSampledBase + kSampledSlots))
		return false;
	m_sampledImages[binding - kSampledBase] = ImageBinding{ target, bufferIndex };
	return true;
}


bool ComputeShader::BindStorageImage(uint32_t binding, RenderTarget* target, int bufferIndex, uint32_t arrayIndex)
{
	(void)arrayIndex;
	if ((binding < kStorageBase) or (binding >= kStorageBase + kStorageSlots))
		return false;
	m_storageImages[binding - kStorageBase] = ImageBinding{ target, bufferIndex };
	return true;
}


bool ComputeShader::Record(CommandList* cmdList, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	ID3D12GraphicsCommandList* list = cmdList->GfxList();
	if (not list)
		return false;

	D3D12_GPU_VIRTUAL_ADDRESS accelStructure = commandListHandler.m_boundAccelStructure;
	if (m_usesAccelStructure and (accelStructure == 0)) {
		logHandler.Print("ComputeShader '%s': declares an acceleration structure, but none is bound\n", static_cast<const char*>(m_name));
		return false;
	}

	ID3D12DescriptorHeap* heaps[] = { descriptorHeaps.SrvHeapPtr(), descriptorHeaps.m_samplerHeap.Ptr() };
	list->SetDescriptorHeaps(2, heaps);
	list->SetComputeRootSignature(m_rootSignature.Get());
	list->SetPipelineState(m_pipeline.Get());

	if (m_cbvRootIndex[0] >= 0) {
		CbAlloc b0a = cbvAllocator.Allocate(UINT(sizeof(FrameConstants)));
		if (not b0a.IsValid())
			return false;
		FrameConstants fc{};
		std::memcpy(fc.mModelView, baseRenderer.ModelView().AsArray(), 64);
		std::memcpy(fc.mProjection, baseRenderer.Projection().AsArray(), 64);
		std::memcpy(fc.mViewport, baseRenderer.ViewportTransformation().AsArray(), 64);
		std::memcpy(b0a.cpu, &fc, sizeof(FrameConstants));
		list->SetComputeRootConstantBufferView(UINT(m_cbvRootIndex[0]), b0a.gpu);
	}

	if (m_cbvRootIndex[1] >= 0) {
		if ((m_b1Size == 0) or not UploadB1())
			return false;
		list->SetComputeRootConstantBufferView(UINT(m_cbvRootIndex[1]), m_b1GpuVA);
	}

	for (uint32_t slot = 0; slot < kSampledSlots; ++slot) {
		if (m_srvRootIndex[slot] < 0)
			continue;
		const ImageBinding& image = m_sampledImages[slot];
		if ((image.target == nullptr) or (image.bufferIndex < 0) or (image.bufferIndex >= image.target->m_bufferCount)) {
			logHandler.Print("ComputeShader '%s': no image bound to register t%u\n", static_cast<const char*>(m_name), slot);
			return false;
		}
		BufferInfo& info = image.target->m_bufferInfo[image.bufferIndex];
		info.SetState(cmdList, kShaderReadState);
		list->SetComputeRootDescriptorTable(UINT(m_srvRootIndex[slot]), info.m_srv.GPUHandle());
	}

	for (uint32_t slot = 0; slot < kStorageSlots; ++slot) {
		if (m_uavRootIndex[slot] < 0)
			continue;
		const ImageBinding& image = m_storageImages[slot];
		if (image.target != nullptr) {
			if ((image.bufferIndex < 0) or (image.bufferIndex >= image.target->m_bufferCount))
				return false;
			BufferInfo& info = image.target->m_bufferInfo[image.bufferIndex];
			if (info.m_type != BufferInfo::btSkyMap) {
				logHandler.Print("ComputeShader '%s': the image bound to register u%u is not a compute buffer\n",
								 static_cast<const char*>(m_name), slot);
				return false;
			}
			info.SetState(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			list->SetComputeRootDescriptorTable(UINT(m_uavRootIndex[slot]), info.m_uav.GPUHandle());
		}
		else if (commandListHandler.m_storageBufferStates[slot].pResource != nullptr)
			list->SetComputeRootDescriptorTable(UINT(m_uavRootIndex[slot]),
												descriptorHeaps.m_srvHeap.GpuHandle(
													commandListHandler.m_boundStorageBuffers[slot]));
		else {
			logHandler.Print("ComputeShader '%s': nothing bound to register u%u\n", static_cast<const char*>(m_name), slot);
			return false;
		}
	}

	for (uint32_t slot = 0; slot < kReadOnlySlots; ++slot) {
		if (m_readOnlyRootIndex[slot] < 0)
			continue;
		if (commandListHandler.m_readOnlyBufferStates[slot].pResource == nullptr) {
			logHandler.Print("ComputeShader '%s': no buffer bound to read only slot %u\n", static_cast<const char*>(m_name), slot);
			return false;
		}
		list->SetComputeRootDescriptorTable(UINT(m_readOnlyRootIndex[slot]),
											descriptorHeaps.m_srvHeap.GpuHandle(commandListHandler.m_boundReadOnlyBuffers[slot]));
	}
	if (m_usesAccelStructure)
		list->SetComputeRootShaderResourceView(UINT(m_accelRootIndex), accelStructure);
	commandListHandler.TransitionBoundBuffers(list);

	list->Dispatch(groupCountX, groupCountY, groupCountZ);

	for (ImageBinding& image : m_storageImages) {
		if ((image.target != nullptr) and (image.bufferIndex >= 0) and (image.bufferIndex < image.target->m_bufferCount))
			image.target->m_bufferInfo[image.bufferIndex].SetState(cmdList, kShaderReadState);
	}
	return true;
}


bool ComputeShader::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	if (not IsValid())
		return false;
	if ((groupCountX == 0) or (groupCountY == 0) or (groupCountZ == 0))
		return false;

	void* opHandle = baseRenderer.StartOperation(m_name, false);
	if (opHandle == nullptr)
		return false;
	CommandList*	cmdList = commandListHandler.CurrentCmdList();
	bool			ok = (cmdList != nullptr) and Record(cmdList, groupCountX, groupCountY, groupCountZ);
	baseRenderer.FinishOperation(opHandle);
	ResetImageBindings();
	return ok;
}


bool ComputeShader::Dispatch2D(uint32_t width, uint32_t height, uint32_t tileX, uint32_t tileY)
{
	if (tileX == 0 or tileY == 0)
		return false;
	return Dispatch((width + tileX - 1) / tileX, (height + tileY - 1) / tileY, 1);
}


// A whole compute pass of its own, outside the frame. The per-frame path leaves root binds and
// Dispatch to the caller because it has the frame's command list; here there is none, so this opens
// one of its own and waits for it - the same route GfxArray::Download () takes. The storage buffers
// are the ones the caller bound through GfxArray::Bind (); an unbound slot has no resource behind it,
// which is what tells the two apart (index 0 is a valid descriptor).

bool ComputeShader::DispatchOnce(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	if (not IsValid())
		return false;
	if ((groupCountX == 0) or (groupCountY == 0) or (groupCountZ == 0))
		return false;

	D3D12_GPU_VIRTUAL_ADDRESS accelStructure = commandListHandler.m_boundAccelStructure;
	if (m_usesAccelStructure and (accelStructure == 0)) {
		logHandler.Print("ComputeShader '%s': declares an acceleration structure, but none is bound\n", (const char*)m_name);
		return false;
	}

	const bool useB1 = (m_b1Size > 0) and (m_cbvRootIndex[1] >= 0);
	if (useB1 and not UploadB1())
		return false;

	CommandList* cl = commandListHandler.CreateCmdList("ComputeShader::DispatchOnce", true);
	if (not cl or not cl->Open())
		return false;

	ID3D12GraphicsCommandList* list = cl->GfxList();
	if (not list)
		return false;

	ID3D12DescriptorHeap* heaps[] = { descriptorHeaps.SrvHeapPtr() };
	list->SetDescriptorHeaps(1, heaps);
	list->SetComputeRootSignature(m_rootSignature.Get());
	list->SetPipelineState(m_pipeline.Get());

	if (useB1)
		list->SetComputeRootConstantBufferView(UINT(m_cbvRootIndex[1]), m_b1GpuVA);

	for (uint32_t slot = 0; slot < CommandList::kUavSlots; ++slot) {
		if (commandListHandler.m_storageBufferStates[slot].pResource == nullptr)
			continue;
		if (m_uavRootIndex[slot] < 0)
			continue;
		list->SetComputeRootDescriptorTable(UINT(m_uavRootIndex[slot]),
											descriptorHeaps.m_srvHeap.GpuHandle(commandListHandler.m_boundStorageBuffers[slot]));
	}
	for (uint32_t slot = 0; slot < kReadOnlySlots; ++slot) {
		if (commandListHandler.m_readOnlyBufferStates[slot].pResource == nullptr)
			continue;
		if (m_readOnlyRootIndex[slot] < 0)
			continue;
		list->SetComputeRootDescriptorTable(UINT(m_readOnlyRootIndex[slot]),
											descriptorHeaps.m_srvHeap.GpuHandle(commandListHandler.m_boundReadOnlyBuffers[slot]));
	}
	if (m_usesAccelStructure)
		list->SetComputeRootShaderResourceView(UINT(m_accelRootIndex), accelStructure);
	commandListHandler.TransitionBoundBuffers(list);

	list->Dispatch(groupCountX, groupCountY, groupCountZ);
	cl->Flush();
	commandListHandler.CmdQueue().WaitIdle();
	return true;
}


int ComputeShader::SetB1(uint32_t offset, const void* data, size_t size)
noexcept
{
	if (offset + size > m_b1Staging.size())
		m_b1Staging.resize(offset + size, 0);
	std::memcpy(m_b1Staging.data() + offset, data, size);
	m_b1Dirty = true;
	return int(offset);
}


int ComputeShader::SetB1Field(const char* name, const void* data, size_t size)
noexcept
{
	for (auto& kv : m_b1Fields) {
		if (kv.first == name) {
			uint32_t offset = kv.second.offset;
			if (offset + size <= m_b1Staging.size()) {
				std::memcpy(m_b1Staging.data() + offset, data, size);
				m_b1Dirty = true;
				return int(offset);
			}
			return -1;
		}
	}
#ifdef _DEBUG
	logHandler.Print("ComputeShader '%s': unknown uniform '%s'\n", (const char*)m_name, name);
#endif
	return -1;
}


int ComputeShader::SetFloat(const char* name, float data)
noexcept
{
	return SetB1Field(name, &data, sizeof(float));
}
int ComputeShader::SetInt(const char* name, int data)
noexcept
{
	return SetB1Field(name, &data, sizeof(int));
}
int ComputeShader::SetVector2f(const char* name, const Vector2f& data)
noexcept
{
	return SetB1Field(name, &data, sizeof(Vector2f));
}
int ComputeShader::SetVector3f(const char* name, const Vector3f& data)
noexcept
{
	return SetB1Field(name, &data, sizeof(Vector3f));
}
int ComputeShader::SetVector4f(const char* name, const Vector4f& data)
noexcept
{
	return SetB1Field(name, &data, sizeof(Vector4f));
}
int ComputeShader::SetVector2i(const char* name, const Vector2i& data)
noexcept
{
	return SetB1Field(name, &data, sizeof(Vector2i));
}
int ComputeShader::SetVector3i(const char* name, const Vector3i& data)
noexcept
{
	return SetB1Field(name, &data, sizeof(Vector3i));
}
int ComputeShader::SetVector4i(const char* name, const Vector4i& data)
noexcept
{
	return SetB1Field(name, &data, sizeof(Vector4i));
}
int ComputeShader::SetMatrix4f(const char* name, const float* data, bool /*transpose*/)
noexcept
{
	return SetB1Field(name, data, 16 * sizeof(float));
}
int ComputeShader::SetMatrix3f(const char* name, const float* data, bool /*transpose*/)
noexcept
{
	return SetB1Field(name, data, 9 * sizeof(float));
}


bool ComputeShader::UploadB1(void)
noexcept
{
	if (m_b1Size == 0)
		return true;
	CbAlloc a = cbvAllocator.Allocate(UINT(m_b1Size));
	if (not a.IsValid())
		return false;
	std::memcpy(a.cpu, m_b1Staging.data(), m_b1Size);
	m_b1GpuVA = a.gpu;
	m_b1Dirty = false;
	return true;
}

// =================================================================================================
