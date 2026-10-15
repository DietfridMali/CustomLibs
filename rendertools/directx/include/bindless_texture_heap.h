#pragma once

#include "dx12framework.h"
#include <d3d12shader.h>
#include "basesingleton.hpp"
#include "bindless_textures.h"
#include "descriptor_heap.h"
#include "rendertypes.h"
#include "texturesampling.h"

#include <cstdint>
#include <vector>

// =================================================================================================

class BindlessTextureHeap
	: public BaseSingleton<BindlessTextureHeap> {
public:
	static constexpr uint32_t kMaxCapacity = DescriptorHeapHandler::BINDLESS_CAPACITY;
	static constexpr uint32_t kInvalidIndex = BindlessTextures::kInvalidIndex;
	static constexpr uint32_t kTable2D = 0;
	static constexpr uint32_t kTable2DArray = 1;
	static constexpr uint32_t kTableCube = 2;
	static constexpr uint32_t kTableSamplers = 3;
	static constexpr uint32_t kTableCount = 4;
	static constexpr uint32_t kImageTableCount = kTableSamplers;
	static constexpr uint32_t kSpace2D = BindlessTextures::kSpace2D;
	static constexpr uint32_t kSpace2DArray = BindlessTextures::kSpace2DArray;
	static constexpr uint32_t kSpaceCube = BindlessTextures::kSpaceCube;
	static constexpr uint32_t kSpaceSamplers = BindlessTextures::kSpaceSamplers;

	static constexpr const char* kHlslDeclarations = BindlessTextures::kHlslDeclarations;

	static inline bool s_isAlive{ false };

	class IndexPool
		: public BaseDescriptorHeap {
	public:
		virtual void Free(uint32_t index)
		noexcept override;
	};

	IndexPool				m_indexPool;
	uint32_t				m_srvBase{ UINT32_MAX };
	uint32_t				m_samplerBase{ UINT32_MAX };
	uint32_t				m_capacity{ 0 };
	uint32_t				m_nextIndex{ 0 };
	std::vector<uint32_t>	m_freeIndices;

	BindlessTextureHeap()
	noexcept
	{
		s_isAlive = true;
	}

	~BindlessTextureHeap()
	noexcept
	{
		s_isAlive = false;
	}

	bool Create(ID3D12Device* device)
	noexcept;

	void Destroy(void)
	noexcept;

	inline bool IsAvailable(void) const
	noexcept
	{
		return m_capacity > 0;
	}

	inline uint32_t Capacity(void) const
	noexcept
	{
		return m_capacity;
	}

	uint32_t Allocate(void)
	noexcept;

	void Free(uint32_t index)
	noexcept;

	bool Write(uint32_t index, TextureType type, uint32_t srvIndex, const TextureSampling& sampling)
	noexcept;

	D3D12_GPU_DESCRIPTOR_HANDLE Table(uint32_t table) const
	noexcept;

	static void DescriptorRanges(D3D12_DESCRIPTOR_RANGE (&ranges)[kTableCount])
	noexcept;

	static bool ReflectUsesHeap(ID3D12ShaderReflection* reflection)
	noexcept;

private:
	void Release(uint32_t index)
	noexcept;
};

#define bindlessTextureHeap BindlessTextureHeap::Instance()

// =================================================================================================
