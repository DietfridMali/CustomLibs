#define NOMINMAX

#include <algorithm>
#include <cstring>

#include "bindless_texture_heap.h"
#include "shader.h"
#include "resource_handler.h"
#include "loghandler.h"
#include <spirv_reflect.h>

// =================================================================================================

namespace {

static const wchar_t* const kBindArgs[] = {
	L"-fvk-bind-register",
	L"t0",
	L"3",
	L"0",
	L"1",
	L"-fvk-bind-register",
	L"t0",
	L"4",
	L"1",
	L"1",
	L"-fvk-bind-register",
	L"t0",
	L"5",
	L"2",
	L"1",
	L"-fvk-bind-register",
	L"s0",
	L"3",
	L"3",
	L"1",
};
static_assert(BindlessTextureHeap::kSet == 1, "kBindArgs names the heap's descriptor set by number");
static_assert(BindlessTextureHeap::kBinding2D == 0, "kBindArgs names the heap's bindings by number");
static_assert(BindlessTextureHeap::kBinding2DArray == 1, "kBindArgs names the heap's bindings by number");
static_assert(BindlessTextureHeap::kBindingCube == 2, "kBindArgs names the heap's bindings by number");
static_assert(BindlessTextureHeap::kBindingSamplers == 3, "kBindArgs names the heap's bindings by number");
static_assert(BindlessTextureHeap::kSpace2D == 3, "kBindArgs names the heap's register spaces by number");
static_assert(BindlessTextureHeap::kSpace2DArray == 4, "kBindArgs names the heap's register spaces by number");
static_assert(BindlessTextureHeap::kSpaceCube == 5, "kBindArgs names the heap's register spaces by number");
static_assert(BindlessTextureHeap::kSpaceSamplers == 3, "kBindArgs names the heap's register spaces by number");

static constexpr uint32_t kBindArgCount = uint32_t(sizeof(kBindArgs) / sizeof(kBindArgs[0]));
static constexpr uint32_t kImageBindingCount = BindlessTextureHeap::kBindingSamplers;
static constexpr const char* kSpaceNames[] = { "space3", "space4", "space5" };

} // namespace

// =================================================================================================

uint32_t BindlessTextureHeap::ComputeCapacity(VkPhysicalDevice physicalDevice) const
noexcept
{
	VkPhysicalDeviceVulkan12Properties props12{};
	props12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
	VkPhysicalDeviceProperties2 props{};
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props.pNext = &props12;
	vkGetPhysicalDeviceProperties2(physicalDevice, &props);

	uint64_t	capacity = kMaxCapacity;
	auto		limit = [&capacity](uint64_t available, uint64_t reserved, uint64_t perEntry) {
		capacity = std::min(capacity, (available > reserved) ? (available - reserved) / perEntry : uint64_t(0));
	};
	limit(props12.maxPerStageDescriptorUpdateAfterBindSampledImages, Shader::kSrvSlots, kImageBindingCount);
	limit(props12.maxDescriptorSetUpdateAfterBindSampledImages, Shader::kSrvSlots, kImageBindingCount);
	limit(props12.maxPerStageDescriptorUpdateAfterBindSamplers, Shader::kSamplerSlots, 1);
	limit(props12.maxDescriptorSetUpdateAfterBindSamplers, Shader::kSamplerSlots, 1);
	limit(props12.maxPerStageUpdateAfterBindResources, Shader::kBindingCount, kBindingCount);
	return uint32_t(capacity);
}


bool BindlessTextureHeap::Create(VkDevice device, VkPhysicalDevice physicalDevice)
noexcept
{
	Destroy();
	if ((device == VK_NULL_HANDLE) or (physicalDevice == VK_NULL_HANDLE))
		return false;

	const uint32_t capacity = ComputeCapacity(physicalDevice);
	if (capacity == 0) {
		logHandler.Print("BindlessTextureHeap::Create: the device limits leave no room for a texture heap\n");
		return false;
	}
	m_device = device;

	VkDescriptorSetLayoutBinding	bindings[kBindingCount]{};
	VkDescriptorBindingFlags		bindingFlags[kBindingCount]{};
	for (uint32_t i = 0; i < kBindingCount; ++i) {
		bindings[i].binding = i;
		bindings[i].descriptorType = (i == kBindingSamplers) ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[i].descriptorCount = capacity;
		bindings[i].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[i].pImmutableSamplers = nullptr;
		bindingFlags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.pNext = nullptr,
		.bindingCount = kBindingCount,
		.pBindingFlags = bindingFlags
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &flagsInfo,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = kBindingCount,
		.pBindings = bindings
	};
	VkResult res = vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_setLayout);
	if (res != VK_SUCCESS) {
		logHandler.Print("BindlessTextureHeap::Create: vkCreateDescriptorSetLayout failed (%d)\n", int(res));
		Destroy();
		return false;
	}

	VkDescriptorPoolSize sizes[2]{};
	sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	sizes[0].descriptorCount = capacity * kImageBindingCount;
	sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
	sizes[1].descriptorCount = capacity;

	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = 1,
		.poolSizeCount = 2,
		.pPoolSizes = sizes
	};
	res = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool);
	if (res != VK_SUCCESS) {
		logHandler.Print("BindlessTextureHeap::Create: vkCreateDescriptorPool failed (%d)\n", int(res));
		Destroy();
		return false;
	}

	VkDescriptorSetAllocateInfo allocInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = m_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &m_setLayout
	};
	res = vkAllocateDescriptorSets(m_device, &allocInfo, &m_set);
	if (res != VK_SUCCESS) {
		logHandler.Print("BindlessTextureHeap::Create: vkAllocateDescriptorSets failed (%d)\n", int(res));
		m_set = VK_NULL_HANDLE;
		Destroy();
		return false;
	}
	m_capacity = capacity;
#ifdef _DEBUG
	logHandler.Print("Vulkan texture heap: %u entries\n", m_capacity);
#endif
	return true;
}


void BindlessTextureHeap::Destroy(void)
noexcept
{
	if (m_device != VK_NULL_HANDLE) {
		if (m_pool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(m_device, m_pool, nullptr);
		if (m_setLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
	}
	m_pool = VK_NULL_HANDLE;
	m_setLayout = VK_NULL_HANDLE;
	m_set = VK_NULL_HANDLE;
	m_device = VK_NULL_HANDLE;
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
	gfxResourceHandler.TrackCleanup([index]() {
		if (BindlessTextureHeap::s_isAlive)
			bindlessTextureHeap.Release(index);
	});
}


void BindlessTextureHeap::Release(uint32_t index)
noexcept
{
	if (IsAvailable() and (index < m_nextIndex))
		m_freeIndices.push_back(index);
}


bool BindlessTextureHeap::Write(uint32_t index, VkImageViewType viewType, VkImageView view, VkImageLayout layout, VkSampler sampler)
noexcept
{
	if (not IsAvailable() or (index >= m_capacity) or (view == VK_NULL_HANDLE) or (sampler == VK_NULL_HANDLE))
		return false;

	uint32_t binding = kBinding2D;
	if (viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY)
		binding = kBinding2DArray;
	else if (viewType == VK_IMAGE_VIEW_TYPE_CUBE)
		binding = kBindingCube;
	else if (viewType != VK_IMAGE_VIEW_TYPE_2D) {
		logHandler.Print("BindlessTextureHeap::Write: view type %d has no place in the heap\n", int(viewType));
		return false;
	}

	VkDescriptorImageInfo imageInfo{
		.sampler = VK_NULL_HANDLE,
		.imageView = view,
		.imageLayout = layout
	};
	VkDescriptorImageInfo samplerInfo{
		.sampler = sampler,
		.imageView = VK_NULL_HANDLE,
		.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	VkWriteDescriptorSet writes[2]{};
	writes[0] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = nullptr,
		.dstSet = m_set,
		.dstBinding = binding,
		.dstArrayElement = index,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
		.pImageInfo = &imageInfo
	};
	writes[1] = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = nullptr,
		.dstSet = m_set,
		.dstBinding = kBindingSamplers,
		.dstArrayElement = index,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
		.pImageInfo = &samplerInfo
	};
	vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
	return true;
}


bool BindlessTextureHeap::DeclaresHeap(const char* hlslCode)
noexcept
{
	if (not hlslCode)
		return false;
	for (const char* spaceName : kSpaceNames) {
		if (std::strstr(hlslCode, spaceName) != nullptr)
			return true;
	}
	return false;
}


bool BindlessTextureHeap::ReflectUsesHeap(const std::vector<uint8_t>& spirv)
noexcept
{
	if (spirv.empty())
		return false;

	SpvReflectShaderModule module{};
	if (spvReflectCreateShaderModule(spirv.size(), spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
		return false;

	uint32_t count = 0;
	spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
	std::vector<SpvReflectDescriptorBinding*> bindings(count);
	spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

	bool usesHeap = false;
	for (SpvReflectDescriptorBinding* b : bindings) {
		if (b and (b->set == kSet))
			usesHeap = true;
	}

	spvReflectDestroyShaderModule(&module);
	return usesHeap;
}


const wchar_t* const* BindlessTextureHeap::BindArgs(uint32_t& count)
noexcept
{
	count = kBindArgCount;
	return kBindArgs;
}

// =================================================================================================
