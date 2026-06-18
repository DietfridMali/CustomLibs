#pragma once

#include "dx12framework.h"
#include "descriptor_heap.h"

// =================================================================================================

static constexpr DXGI_FORMAT dxColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT dxVertexFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
// Single-channel D32_FLOAT statt D24_UNORM_S8_UINT: kein Stencil, kein Treiber-Padding-Overhead.
// Typeless-Resource ist R32_TYPELESS, DSV ist D32_FLOAT, SRV ist R32_FLOAT.
static constexpr DXGI_FORMAT dxTypelessDepthFormat = DXGI_FORMAT_R32_TYPELESS;
static constexpr DXGI_FORMAT dxDepthDSVFormat = DXGI_FORMAT_D32_FLOAT;
static constexpr DXGI_FORMAT dxDepthSRVFormat = DXGI_FORMAT_R32_FLOAT;

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

	// Releases the descriptor through its owning heap (DescriptorHandle::m_heap) and clears the handle.
	void Free(void) noexcept;

};


class RTV
	: public ResourceView 
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format = {}) override;
};


class SRV
	: public ResourceView
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format = {}) override;
};


class DSV
	: public ResourceView
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format = {}) override;

	// Read-only / sampleable variant: pass D3D12_DSV_FLAG_READ_ONLY_DEPTH to bind the depth as a
	// read-only DSV (compare, no write) so the same resource may be sampled as an SRV in the same pass.
	bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format, D3D12_DSV_FLAGS flags);
};


class UAV
	: public ResourceView
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format = {}) override;
};

// =================================================================================================
