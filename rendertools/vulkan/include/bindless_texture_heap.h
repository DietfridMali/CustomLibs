#pragma once

#include "vkframework.h"
#include "basesingleton.hpp"

#include <cstdint>
#include <vector>

// =================================================================================================

class BindlessTextureHeap
	: public BaseSingleton<BindlessTextureHeap> {
public:
	static constexpr uint32_t kMaxCapacity = 16384;
	static constexpr uint32_t kInvalidIndex = UINT32_MAX;
	static constexpr uint32_t kSet = 1;
	static constexpr uint32_t kBinding2D = 0;
	static constexpr uint32_t kBinding2DArray = 1;
	static constexpr uint32_t kBindingCube = 2;
	static constexpr uint32_t kBindingSamplers = 3;
	static constexpr uint32_t kBindingCount = 4;
	static constexpr uint32_t kSpace2D = 3;
	static constexpr uint32_t kSpace2DArray = 4;
	static constexpr uint32_t kSpaceCube = 5;
	static constexpr uint32_t kSpaceSamplers = 3;

	static constexpr const char* kHlslDeclarations = R"(
Texture2D		bindless2D [] : register(t0, space3);
Texture2DArray	bindless2DArray [] : register(t0, space4);
TextureCube		bindlessCube [] : register(t0, space5);
SamplerState	bindlessSamplers [] : register(s0, space3);
)";

	static inline bool s_isAlive{ false };

	VkDevice				m_device{ VK_NULL_HANDLE };
	VkDescriptorPool		m_pool{ VK_NULL_HANDLE };
	VkDescriptorSetLayout	m_setLayout{ VK_NULL_HANDLE };
	VkDescriptorSet			m_set{ VK_NULL_HANDLE };
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

	bool Create(VkDevice device, VkPhysicalDevice physicalDevice)
	noexcept;

	void Destroy(void)
	noexcept;

	inline bool IsAvailable(void) const
	noexcept
	{
		return m_set != VK_NULL_HANDLE;
	}

	inline VkDescriptorSetLayout SetLayout(void) const
	noexcept
	{
		return m_setLayout;
	}

	inline VkDescriptorSet Set(void) const
	noexcept
	{
		return m_set;
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

	bool Write(uint32_t index, VkImageViewType viewType, VkImageView view, VkImageLayout layout, VkSampler sampler)
	noexcept;

	static bool DeclaresHeap(const char* hlslCode)
	noexcept;

	static bool ReflectUsesHeap(const std::vector<uint8_t>& spirv)
	noexcept;

	static const wchar_t* const* BindArgs(uint32_t& count)
	noexcept;

private:
	uint32_t ComputeCapacity(VkPhysicalDevice physicalDevice) const
	noexcept;

	void Release(uint32_t index)
	noexcept;
};

#define bindlessTextureHeap BindlessTextureHeap::Instance()

// =================================================================================================
