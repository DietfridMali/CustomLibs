#pragma once

#include "dx12framework.h"
#include "descriptor_heap.h"

// =================================================================================================

static constexpr DXGI_FORMAT dxColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT dxVertexFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
static constexpr DXGI_FORMAT dxTypelessDepthFormat = DXGI_FORMAT_R24G8_TYPELESS;
static constexpr DXGI_FORMAT dxDepthDSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
static constexpr DXGI_FORMAT dxDepthSRVFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

// -------------------------------------------------------------------------------------------------

class ResourceView
	: protected DescriptorHandle
{
public:
	inline bool IsValid(void) noexcept {
		return DescriptorHandle::IsValid();
	}

	inline D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle(void) noexcept {
		return cpuHandle;
	}

	inline D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle(void) const noexcept {
		return cpuHandle;
	}

	inline D3D12_CPU_DESCRIPTOR_HANDLE* CPUHandleAddress(void) noexcept {
		return &cpuHandle;
	}

	inline D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle(void) noexcept {
		return gpuHandle;
	}

	inline uint32_t GetIndex(void) noexcept {
		return index;
	}

	inline uint32_t& Index(void) noexcept {
		return index;
	}

	inline DescriptorHandle& Handle(void) noexcept {
		return *static_cast<DescriptorHandle*>(this);
	}

	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format = {}) = 0;

	virtual void Free(void) = 0;

};


class RTV
	: public ResourceView 
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format = {}) override;

	virtual void Free(void) override;
};


class SRV
	: public ResourceView
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format = {}) override;

	virtual void Free(void) override;
};


class DSV
	: public ResourceView
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format = {}) override;

	virtual void Free(void) override;
};

// =================================================================================================
