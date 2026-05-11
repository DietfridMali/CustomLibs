#pragma once

#include "dx12framework.h"
#include "descriptor_heap.h"

// =================================================================================================

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

	inline D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle(void) noexcept {
		return gpuHandle;
	}

	inline UINT Index(void) noexcept {
		return index;
	}

	inline DescriptorHandle& Handle(void) noexcept {
		return *static_cast<DescriptorHandle*>(this);
	}

	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format) = 0;

	virtual void Free(void) = 0;

};


class RTV
	: public ResourceView 
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format) override;

	virtual void Free(void) override;
};


class SRV
	: public ResourceView
{
public:
	virtual bool Create(ComPtr<ID3D12Resource> resource, DXGI_FORMAT format) override;

	virtual void Free(void) override;
};

// =================================================================================================
