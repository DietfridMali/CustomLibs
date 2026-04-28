#pragma once

#include "dx12framework.h"
#include "basesingleton.hpp"
#include "array.hpp"

// =================================================================================================
// DescriptorHandle: a combined CPU+GPU handle plus the linear slot index.

struct DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{ 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{ 0 };
    UINT                        index{ UINT_MAX };

    inline bool IsValid(void) const noexcept { return index != UINT_MAX; }
};

// =================================================================================================
// DescriptorHeap: fixed-size descriptor heap with linear (bump-pointer) allocation.
// Individual slots are never freed; use for persistent per-resource registrations.

class DescriptorHeap {
public:
    ComPtr<ID3D12DescriptorHeap>    m_heap;
    D3D12_DESCRIPTOR_HEAP_TYPE      m_type{};
    UINT                            m_capacity{ 0 };
    UINT                            m_count{ 0 };
    UINT                            m_descriptorSize{ 0 };
    bool                            m_gpuVisible{ false };
    AutoArray<UINT>                 m_freeList;

    bool Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                UINT capacity, bool gpuVisible = false) noexcept;

    // Allocates the next free slot (reuses freed slots). Returns an invalid handle if the heap is full.
    DescriptorHandle Allocate(void) noexcept;
    // Returns a slot to the free list so it can be reused by a future Allocate().
    void Free(UINT index) noexcept;
    
    inline void Free(const DescriptorHandle& h) noexcept { 
        if (h.IsValid()) 
            Free(h.index); 
    }

    inline bool IsFull(void) const noexcept { 
        return m_freeList.Length() == 0 && m_count >= m_capacity; 
    }
    
    inline UINT Remaining(void) const noexcept { 
        return (m_capacity - m_count) + UINT(m_freeList.Length()); 
    }

    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(UINT index) const noexcept;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(UINT index) const noexcept;

    inline ID3D12DescriptorHeap* Ptr(void) const noexcept { return m_heap.Get(); }
};

// =================================================================================================
// DescriptorHeapHandler: singleton owning one heap per descriptor type.
// Capacity constants are intentionally generous; adjust if a project requires more.

class DescriptorHeapHandler 
    : public BaseSingleton<DescriptorHeapHandler>
{
public:
    static constexpr UINT RTV_CAPACITY = 64;
    static constexpr UINT DSV_CAPACITY = 16;
    static constexpr UINT SRV_CAPACITY = 1024; // CBV/SRV/UAV, GPU-visible

    DescriptorHeap m_rtvHeap;
    DescriptorHeap m_dsvHeap;
    DescriptorHeap m_srvHeap;

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

    inline void FreeRTV(const DescriptorHandle& h) noexcept { 
        m_rtvHeap.Free(h); 
    }
    
    inline void FreeDSV(const DescriptorHandle& h) noexcept { 
        m_dsvHeap.Free(h); 
    }
    
    inline void FreeSRV(const DescriptorHandle& h) noexcept { 
        m_srvHeap.Free(h); 
    }

    // The GPU-visible SRV heap must be bound before any draw call.
    inline ID3D12DescriptorHeap* SrvHeapPtr(void) const noexcept { 
        return m_srvHeap.Ptr(); 
    }
};

#define descriptorHeaps DescriptorHeapHandler::Instance()

// =================================================================================================
