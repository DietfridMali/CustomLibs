#pragma once

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "array.hpp"

// =================================================================================================
// DescriptorHandle: a combined CPU+GPU handle plus the linear slot index.

struct DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{ 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{ 0 };
    uint32_t                    index{ UINT32_MAX };

    inline bool IsValid(void) const noexcept { 
        return index != UINT32_MAX;
    }

    inline void SetIndex(uint32_t value) noexcept {
        index = value;
    }
};

// =================================================================================================
// DescriptorHeap: fixed-size descriptor heap with linear (bump-pointer) allocation.
// Individual slots are never freed; use for persistent per-resource registrations.

class DescriptorHeap {
public:
    ComPtr<ID3D12DescriptorHeap>    m_heap;
    D3D12_DESCRIPTOR_HEAP_TYPE      m_type{};
    uint32_t                        m_capacity{ 0 };
    uint32_t                        m_count{ 0 };
    uint32_t                        m_descriptorSize{ 0 };
    bool                            m_gpuVisible{ false };
    AutoArray<uint32_t>             m_freeList;

    bool Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool gpuVisible = false) noexcept;

    // Allocates the next free slot (reuses freed slots). Returns an invalid handle if the heap is full.
    DescriptorHandle Allocate(void) noexcept;
    // Returns a slot to the free list so it can be reused by a future Allocate().
    void Free(uint32_t index) noexcept;
    
    inline void Free(const DescriptorHandle& h) noexcept { 
        if (h.IsValid()) 
            Free(h.index); 
    }

    inline bool IsFull(void) const noexcept { 
        return m_freeList.Length() == 0 && m_count >= m_capacity; 
    }
    
    inline uint32_t Remaining(void) const noexcept { 
        return (m_capacity - m_count) + uint32_t(m_freeList.Length()); 
    }

    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(uint32_t index) const noexcept;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(uint32_t index) const noexcept;

    inline ID3D12DescriptorHeap* Ptr(void) const noexcept { return m_heap.Get(); }
};

// =================================================================================================
// DescriptorHeapHandler: singleton owning one heap per descriptor type.
// Capacity constants are intentionally generous; adjust if a project requires more.

class DescriptorHeapHandler 
    : public BaseSingleton<DescriptorHeapHandler>
{
public:
    static constexpr uint32_t RTV_CAPACITY     = 64;
    static constexpr uint32_t DSV_CAPACITY     = 16;
    static constexpr uint32_t SRV_CAPACITY     = 1024; // CBV/SRV/UAV, GPU-visible
    static constexpr uint32_t SAMPLER_CAPACITY = 32;   // GPU-visible sampler heap; small — only unique configurations

    DescriptorHeap m_rtvHeap;
    DescriptorHeap m_dsvHeap;
    DescriptorHeap m_srvHeap;
    DescriptorHeap m_samplerHeap;

    bool Create(ID3D12Device* device) noexcept;

    inline DescriptorHandle AllocRTV(void) noexcept { 
        return m_rtvHeap.Allocate(); 
    }
    
    inline DescriptorHandle AllocDSV(void) noexcept { 
        return m_dsvHeap.Allocate(); 
    }
    
    inline DescriptorHandle AllocSRV(void) noexcept {
        return m_srvHeap.Allocate();
    }

    inline DescriptorHandle AllocSampler(void) noexcept {
        return m_samplerHeap.Allocate();
    }

    inline void FreeRTV(const DescriptorHandle& h) noexcept {
        m_rtvHeap.Free(h);
    }

    inline void FreeDSV(const DescriptorHandle& h) noexcept {
        m_dsvHeap.Free(h);
    }

    inline void FreeSRV(const DescriptorHandle& h) noexcept {
        m_srvHeap.Free(h);
    }

    inline void FreeSampler(const DescriptorHandle& h) noexcept {
        m_samplerHeap.Free(h);
    }

    // The GPU-visible SRV heap must be bound before any draw call.
    inline ID3D12DescriptorHeap* SrvHeapPtr(void) const noexcept {
        return m_srvHeap.Ptr();
    }

    // Sampler heap must also be bound (alongside the SRV heap) before any draw call
    // that issues a SetGraphicsRootDescriptorTable for a sampler slot.
    inline ID3D12DescriptorHeap* SamplerHeapPtr(void) const noexcept {
        return m_samplerHeap.Ptr();
    }
};

#define descriptorHeaps DescriptorHeapHandler::Instance()

// =================================================================================================
