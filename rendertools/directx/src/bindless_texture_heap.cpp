#define NOMINMAX

#include <climits>

#include "bindless_texture_heap.h"
#include "descriptor_heap.h"
#include "sampler_cache.h"
#include "resource_handler.h"
#include "dx12context.h"
#include "loghandler.h"

// =================================================================================================

static_assert(BindlessTextureHeap::kImageTableCount == DescriptorHeapHandler::BINDLESS_SRV_TYPES,
			  "the SRV heap reserves one block per image table of the texture heap");
static_assert(DescriptorHeapHandler::BINDLESS_CAPACITY < DescriptorHeapHandler::SAMPLER_CAPACITY,
			  "the texture heap's samplers must leave room in the sampler heap for the sampler cache");

// =================================================================================================

void BindlessTextureHeap::IndexPool::Free(uint32_t index)
noexcept
{
	if (BindlessTextureHeap::s_isAlive)
		bindlessTextureHeap.Release(index);
}

// =================================================================================================

bool BindlessTextureHeap::Create(ID3D12Device* device)
noexcept
{
	Destroy();
	if (not device)
		return false;

	DescriptorHeap&	srvHeap = descriptorHeaps.m_srvHeap;
	DescriptorHeap&	samplerHeap = descriptorHeaps.m_samplerHeap;
	const uint32_t	capacity = kMaxCapacity;
	const uint32_t	srvCount = kImageTableCount * capacity;

	if ((srvHeap.m_capacity - srvHeap.m_count < srvCount) or (samplerHeap.m_capacity - samplerHeap.m_count < capacity)) {
		logHandler.Print("BindlessTextureHeap::Create: the descriptor heaps leave no room for a texture heap of %u entries\n", capacity);
		return false;
	}

	const uint32_t srvBase = srvHeap.Reserve(srvCount);
	const uint32_t samplerBase = samplerHeap.Reserve(capacity);
	if ((srvBase == UINT32_MAX) or (samplerBase == UINT32_MAX)) {
		logHandler.Print("BindlessTextureHeap::Create: the descriptor blocks could not be reserved\n");
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC descs[kImageTableCount]{};
	for (D3D12_SHADER_RESOURCE_VIEW_DESC& desc : descs) {
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	}
	descs[kTable2D].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	descs[kTable2D].Texture2D.MipLevels = 1;
	descs[kTable2DArray].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	descs[kTable2DArray].Texture2DArray.MipLevels = 1;
	descs[kTable2DArray].Texture2DArray.ArraySize = 1;
	descs[kTableCube].ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	descs[kTableCube].TextureCube.MipLevels = 1;

	for (uint32_t table = 0; table < kImageTableCount; ++table) {
		for (uint32_t i = 0; i < capacity; ++i)
			device->CreateShaderResourceView(nullptr, &descs[table], srvHeap.CpuHandle(srvBase + table * capacity + i));
	}
	srvHeap.Publish(srvBase, srvCount);

	const D3D12_SAMPLER_DESC samplerDesc = SamplerCache::ToD3D12Desc(TextureSampling{});
	for (uint32_t i = 0; i < capacity; ++i)
		device->CreateSampler(&samplerDesc, samplerHeap.CpuHandle(samplerBase + i));

	m_srvBase = srvBase;
	m_samplerBase = samplerBase;
	m_capacity = capacity;
#ifdef _DEBUG
	logHandler.Print("DirectX texture heap: %u entries\n", m_capacity);
#endif
	return true;
}


void BindlessTextureHeap::Destroy(void)
noexcept
{
	m_srvBase = UINT32_MAX;
	m_samplerBase = UINT32_MAX;
	m_capacity = 0;
	m_nextIndex = 0;
	m_freeIndices.clear();
}


uint32_t BindlessTextureHeap::Allocate(void)
noexcept
{
	if (not IsAvailable())
		return kInvalidIndex;
	if (not m_freeIndices.empty()) {
		const uint32_t index = m_freeIndices.back();
		m_freeIndices.pop_back();
		return index;
	}
	if (m_nextIndex >= m_capacity) {
		logHandler.Print("BindlessTextureHeap::Allocate: all %u entries are taken\n", m_capacity);
		return kInvalidIndex;
	}
	return m_nextIndex++;
}


void BindlessTextureHeap::Free(uint32_t index)
noexcept
{
	if ((index == kInvalidIndex) or not IsAvailable())
		return;

	DescriptorHandle handle;
	handle.index = index;
	handle.m_heap = &m_indexPool;
	gfxResourceHandler.Track(handle);
}


void BindlessTextureHeap::Release(uint32_t index)
noexcept
{
	if (IsAvailable() and (index < m_nextIndex))
		m_freeIndices.push_back(index);
}


bool BindlessTextureHeap::Write(uint32_t index, TextureType type, uint32_t srvIndex, const TextureSampling& sampling)
noexcept
{
	ID3D12Device* device = dx12Context.Device();
	if (not device or not IsAvailable() or (index >= m_capacity) or (srvIndex == UINT32_MAX))
		return false;

	uint32_t table = kTable2D;
	if (type == TextureType::Texture2DArray)
		table = kTable2DArray;
	else if (type == TextureType::CubeMap)
		table = kTableCube;
	else if (type != TextureType::Texture2D) {
		logHandler.Print("BindlessTextureHeap::Write: texture type %d has no place in the heap\n", int(type));
		return false;
	}

	DescriptorHeap&	srvHeap = descriptorHeaps.m_srvHeap;
	const uint32_t	slot = m_srvBase + table * m_capacity + index;

	device->CopyDescriptorsSimple(1, srvHeap.CpuHandle(slot), srvHeap.CpuHandle(srvIndex), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	srvHeap.Publish(slot);

	const D3D12_SAMPLER_DESC samplerDesc = SamplerCache::ToD3D12Desc(sampling);
	device->CreateSampler(&samplerDesc, descriptorHeaps.m_samplerHeap.CpuHandle(m_samplerBase + index));
	return true;
}


D3D12_GPU_DESCRIPTOR_HANDLE BindlessTextureHeap::Table(uint32_t table) const
noexcept
{
	if (table == kTableSamplers)
		return descriptorHeaps.m_samplerHeap.GpuHandle(m_samplerBase);
	return descriptorHeaps.m_srvHeap.GpuHandle(m_srvBase + table * m_capacity);
}


void BindlessTextureHeap::DescriptorRanges(D3D12_DESCRIPTOR_RANGE (&ranges)[kTableCount])
noexcept
{
	const uint32_t spaces[kTableCount] = { kSpace2D, kSpace2DArray, kSpaceCube, kSpaceSamplers };

	for (uint32_t table = 0; table < kTableCount; ++table) {
		ranges[table].RangeType = (table == kTableSamplers) ? D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[table].NumDescriptors = UINT_MAX;
		ranges[table].BaseShaderRegister = 0;
		ranges[table].RegisterSpace = spaces[table];
		ranges[table].OffsetInDescriptorsFromTableStart = 0;
	}
}


bool BindlessTextureHeap::ReflectUsesHeap(ID3D12ShaderReflection* reflection)
noexcept
{
	if (not reflection)
		return false;

	D3D12_SHADER_DESC sd{};
	if (FAILED(reflection->GetDesc(&sd)))
		return false;
	for (UINT i = 0; i < sd.BoundResources; ++i) {
		D3D12_SHADER_INPUT_BIND_DESC bd{};
		if (FAILED(reflection->GetResourceBindingDesc(i, &bd)))
			continue;
		if ((bd.Type == D3D_SIT_TEXTURE) and ((bd.Space == kSpace2D) or (bd.Space == kSpace2DArray) or (bd.Space == kSpaceCube)))
			return true;
		if ((bd.Type == D3D_SIT_SAMPLER) and (bd.Space == kSpaceSamplers))
			return true;
	}
	return false;
}

// =================================================================================================
