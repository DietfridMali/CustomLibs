#include "vkframework.h"
#include "commandlist.h"
#include "descriptor_pool_handler.h"
#include "cbv_allocator.h"
#include "resource_handler.h"
#include "gfxstates.h"
#include "shader_loading.h"
#include "loghandler.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <chrono>
#if VK_STALL_DIAG
#include <string>
#include <vector>
#endif

// A lost device cannot be recovered from here: every later submit, wait and present fails as well, and
// an app that keeps recording runs into the driver and the validation layers with dead handles. So the
// first VK_ERROR_DEVICE_LOST ends the program with a message. Defined below, behind the second include
// block; vkupload.cpp declares it extern.
void HandleDeviceLost(VkResult res, const char* where)
noexcept;

static void VkGpuFrameResolve(VkDevice device, uint32_t slotIndex, uint64_t frameNumber)
noexcept;

static void VkGpuFrameAdvance(uint32_t slotIndex)
noexcept;

static void VkGpuFrameDestroy(VkDevice device)
noexcept;

#if VK_STALL_DIAG

static constexpr double kStallFrameFactor = 2.5;
static constexpr double kStallDetailMinMs = 1.0;
static constexpr size_t kStallDetailMaxChars = 16384;
static constexpr uint64_t kSampleFrameInterval = 30;
static constexpr double kSampleDetailMinMs = 0.1;
static constexpr uint64_t kStallFollowFrames = 4;
static constexpr uint32_t kStallRunMax = 120;

struct VkStallEntry {
	const char*	what;
	int			count;
	double		totalMs;
	double		maxMs;
};

struct VkStallRecorder {
	uint64_t					frameNumber{ 0 };
	double						frameStartMs{ 0.0 };
	double						avgFrameMs{ 0.0 };
	double						avgGpuMs{ 0.0 };
	uint32_t					stageMarks{ 0 };
	bool						hasStall{ false };
	uint64_t					lastStallFrame{ 0 };
	uint32_t					stallRun{ 0 };
	uint32_t					gpuStallRun{ 0 };
	bool						wantsConstants{ false };
	char						constantsLabel[48]{};
	std::vector<VkStallEntry>	entries;
	std::string					details;
};

static VkStallRecorder vkStalls;


double VkStallClock(void)
noexcept
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


void VkStallNote(const char* what, double startMs, const char* detail)
noexcept
{
	double			ms = VkStallClock() - startMs;
	VkStallEntry*	entry = nullptr;
	for (auto& e : vkStalls.entries) {
		if (std::strcmp(e.what, what) == 0) {
			entry = &e;
			break;
		}
	}
	if (entry == nullptr) {
		vkStalls.entries.push_back(VkStallEntry{ what, 0, 0.0, 0.0 });
		entry = &vkStalls.entries.back();
	}
	++entry->count;
	entry->totalMs += ms;
	if (entry->maxMs < ms)
		entry->maxMs = ms;
	if ((ms >= kStallDetailMinMs) and (vkStalls.details.size() < kStallDetailMaxChars)) {
		char line[256];
		snprintf(line, sizeof(line), "      %-28s %8.2f ms  %s\n", what, ms, detail ? detail : "");
		vkStalls.details += line;
	}
}


void VkStallEvent(const char* what, double startMs, const char* detail)
noexcept
{
	double ms = VkStallClock() - startMs;
	VkStallNote(what, startMs, detail);
	logHandler.Print("frame %llu: %s %.2f ms  %s\n", static_cast<unsigned long long>(vkStalls.frameNumber), what, ms, detail ? detail : "");
}


static void VkStallPrintHeaps(void)
noexcept;


static void VkStallEndFrame(uint64_t nextFrame)
noexcept
{
	double now = VkStallClock();
	if (vkStalls.frameStartMs > 0.0) {
		double	frameMs = now - vkStalls.frameStartMs;
		bool	isStall = (vkStalls.avgFrameMs > 0.0) and (frameMs > kStallFrameFactor * vkStalls.avgFrameMs);
		if (not isStall)
			vkStalls.stallRun = 0;
		else if (++vkStalls.stallRun > kStallRunMax) {
			vkStalls.avgFrameMs = frameMs;
			vkStalls.stallRun = 0;
			isStall = false;
		}
		if (isStall) {
			vkStalls.hasStall = true;
			vkStalls.lastStallFrame = vkStalls.frameNumber;
			logHandler.Print("STALL frame %llu: %.2f ms (avg %.2f ms)\n", static_cast<unsigned long long>(vkStalls.frameNumber),
							 frameMs, vkStalls.avgFrameMs);
			for (const auto& e : vkStalls.entries)
				logHandler.Print("    %-28s %5d x  total %8.2f ms  max %8.2f ms\n", e.what, e.count, e.totalMs, e.maxMs);
			logHandler.Print("%s", vkStalls.details.c_str());
			VkStallPrintHeaps();
		}
		else if (vkStalls.avgFrameMs == 0.0)
			vkStalls.avgFrameMs = frameMs;
		else
			vkStalls.avgFrameMs = vkStalls.avgFrameMs * 0.95 + frameMs * 0.05;
		if (not isStall and ((vkStalls.frameNumber % kSampleFrameInterval) == 0) and (vkStalls.stageMarks > 0)) {
			std::string	sample;
			char		text[128];
			snprintf(text, sizeof(text), "SAMPLE frame %llu: %.2f ms (avg %.2f ms)", static_cast<unsigned long long>(vkStalls.frameNumber),
					 frameMs, vkStalls.avgFrameMs);
			sample = text;
			for (const auto& e : vkStalls.entries) {
				if (e.totalMs < kSampleDetailMinMs)
					continue;
				snprintf(text, sizeof(text), ", %s %.2f", e.what, e.totalMs);
				sample += text;
			}
			sample += "\n";
			logHandler.Print("%s", sample.c_str());
			VkStallPrintHeaps();
		}
	}
	vkStalls.stageMarks = 0;
	vkStalls.wantsConstants = false;
	vkStalls.entries.clear();
	vkStalls.details.clear();
	vkStalls.frameStartMs = now;
	vkStalls.frameNumber = nextFrame;
}


static bool VkStallWantsConstants(void)
noexcept
{
	if ((vkStalls.frameNumber % kSampleFrameInterval) == 0)
		return true;
	return vkStalls.hasStall and (vkStalls.frameNumber >= vkStalls.lastStallFrame) and
		(vkStalls.frameNumber - vkStalls.lastStallFrame <= kStallFollowFrames);
}


bool VkStallTakeConstantsRequest(char* label, size_t size)
noexcept
{
	if (not vkStalls.wantsConstants)
		return false;
	vkStalls.wantsConstants = false;
	snprintf(label, size, "%s", vkStalls.constantsLabel);
	return true;
}


static void VkGpuPassResolve(VkDevice device, uint32_t slotIndex, uint64_t frameNumber)
noexcept;

static void VkGpuPassAdvance(uint32_t slotIndex)
noexcept;

static void VkGpuPassDestroy(VkDevice device)
noexcept;


// The memory types and heaps of the device, by name - what the memory lines below say about where an
// allocation went.

static const VkPhysicalDeviceMemoryProperties& VkStallMemoryProperties(void)
noexcept
{
	static VkPhysicalDeviceMemoryProperties	props{};
	static bool								loaded = false;
	if (not loaded and (vkContext.PhysicalDevice() != VK_NULL_HANDLE)) {
		vkGetPhysicalDeviceMemoryProperties(vkContext.PhysicalDevice(), &props);
		loaded = true;
	}
	return props;
}


static void VkStallMemoryFlagsText(VkMemoryPropertyFlags flags, char* text, size_t size)
noexcept
{
	snprintf(text, size, "%s%s%s%s%s",
			 (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL" : "SYSTEM",
			 (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? "|HOST_VISIBLE" : "",
			 (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? "|HOST_COHERENT" : "",
			 (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? "|HOST_CACHED" : "",
			 (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) ? "|LAZY" : "");
}


// What every heap holds right now, as the driver sees it (VK_EXT_memory_budget, where the device has
// it) and as VMA counts it. Printed with every STALL and SAMPLE block: an allocation that moves between
// heaps, or a heap whose usage jumps, shows up here next to the frame it happened in.

static void VkStallPrintHeaps(void)
noexcept
{
	VmaAllocator allocator = vkContext.Allocator();
	if (allocator == VK_NULL_HANDLE)
		return;
	const VkPhysicalDeviceMemoryProperties&	props = VkStallMemoryProperties();
	VmaBudget								budgets[VK_MAX_MEMORY_HEAPS]{};
	vmaGetHeapBudgets(allocator, budgets);
	for (uint32_t i = 0; i < props.memoryHeapCount; ++i) {
		const VmaBudget& b = budgets[i];
		logHandler.Print("    heap %u %s %llu MB: usage %llu MB of budget %llu MB, VMA blocks %llu MB in %u, allocations %llu MB in %u\n", i,
						 (props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL" : "SYSTEM",
						 static_cast<unsigned long long>(props.memoryHeaps[i].size >> 20), static_cast<unsigned long long>(b.usage >> 20),
						 static_cast<unsigned long long>(b.budget >> 20), static_cast<unsigned long long>(b.statistics.blockBytes >> 20),
						 unsigned(b.statistics.blockCount), static_cast<unsigned long long>(b.statistics.allocationBytes >> 20),
						 unsigned(b.statistics.allocationCount));
	}
}

#endif

// =================================================================================================
// Where the memory goes. Defined whether or not VK_STALL_DIAG is on, so that the headers (gfxarray.hpp)
// and the application can call them without knowing how the library was built; without the diagnostics
// they do nothing.
//
// One line per VkDeviceMemory the driver hands out or takes back (the VMA callbacks, vkcontext.cpp),
// and one per buffer, array, texture and render target image at creation, each with the memory type
// and heap it landed in. What they are for: a shader that suddenly runs ten times slower on the same
// data is reading memory that is slower than it was - and these lines say which allocations exist, in
// which heap, and what the driver did to the heaps around the frame it happened in.

uint64_t VkStallFrameNumber(void)
noexcept
{
#if VK_STALL_DIAG
	return vkStalls.frameNumber;
#else
	return 0;
#endif
}


void VkStallMemoryEvent(const char* what, VkDeviceSize bytes, uint32_t memoryType, const char* detail)
noexcept
{
#if VK_STALL_DIAG
	const VkPhysicalDeviceMemoryProperties&	props = VkStallMemoryProperties();
	uint32_t								heap = (memoryType < props.memoryTypeCount) ? props.memoryTypes[memoryType].heapIndex : 0;
	char									flags[96];
	VkStallMemoryFlagsText((memoryType < props.memoryTypeCount) ? props.memoryTypes[memoryType].propertyFlags : 0, flags, sizeof(flags));
	logHandler.Print("frame %llu: memory %s %llu KB, type %u heap %u %s%s%s\n", static_cast<unsigned long long>(vkStalls.frameNumber), what,
					 static_cast<unsigned long long>((bytes + 1023) >> 10), memoryType, heap, flags, detail ? "  " : "", detail ? detail : "");
#else
	(void)what;
	(void)bytes;
	(void)memoryType;
	(void)detail;
#endif
}


void VkStallAllocation(const char* what, VmaAllocation allocation, VkDeviceSize bytes, const char* detail)
noexcept
{
#if VK_STALL_DIAG
	VmaAllocator allocator = vkContext.Allocator();
	if ((allocator == VK_NULL_HANDLE) or (allocation == VK_NULL_HANDLE))
		return;
	VmaAllocationInfo info{};
	vmaGetAllocationInfo(allocator, allocation, &info);
	VkStallMemoryEvent(what, bytes ? bytes : info.size, info.memoryType, detail);
#else
	(void)what;
	(void)allocation;
	(void)bytes;
	(void)detail;
#endif
}

// CLs sind die wesentliche Datenstruktur zur Abwicklung von "Render Tasks".
// Render Tasks liegen immer zwischen open und close einer CL. Es gibt in dem Sinne keine verschachtelten Render-Tasks.
// Auch bei geschachteltem open - close von CLs wird die zuerst ausgeführt, die zuerst geschlossen wird - das liegt daran,
// dass diese zuerst in die pending-CL-Liste des CL-Handlers eingetragen wird. Dadurch wird der Vulkan-Port erleichtert.
// RenderTargets haben fixe CLs, diverse Detail-Tasks (i.d.R. Daten-Uploads) holen sich bei Bedarf eine temporäre CL.
// Temporäre CLs wandern nach Ausführung in einen Pool und werden bei Anforderung von temp.CLs bevorzugt verwendet.
// CLs verwalten auch Ressourcen, die im CL-Scope liegen, insb. PSOs für Shader.
// Temporäre CLs sind für den Fall in-Frame wiederholter Render Tasks für dasselbe Renderobjekt (i.d.R. Mesh-Datenpuffer) gedacht
// und nehmen der Objekt-Instanz die Aufgabe ab, hier eine eigene CL-Verwaltung zu implementieren, damit für jede solche Task
// auch eine CL verfügbar ist.

// =================================================================================================
// CommandQueue — Vulkan FrameSync wrapper.
//
// Holds the graphics + present queue handles, per-slot swapchain sync objects
// (imageAvailable, renderFinished, inFlight), and the round-robin frame slot index.
//
// Lifecycle:
//   Create            — record device + queue handles + family indices (no GPU work)
//   InitSyncObjects   — allocate per-slot semaphores + fences (after swapchain exists)
//   BeginFrame        — wait for slot fence, acquire next swapchain image
//   EndFrame          — present, advance frame slot
//   WaitIdle          — vkQueueWaitIdle on graphics queue (local; DX12-equivalent)
//   Destroy           — WaitIdle + destroy sync objects

bool CommandQueue::Create(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
						  uint32_t graphicsFamily, uint32_t presentFamily,
						  const String& name)
noexcept
{
	if ((device == VK_NULL_HANDLE) or (graphicsQueue == VK_NULL_HANDLE) or (presentQueue == VK_NULL_HANDLE)) {
		logHandler.Print("CommandQueue::Create: null device or queue handle\n");
		return false;
	}
	m_device = device;
	m_graphicsQueue = graphicsQueue;
	m_presentQueue = presentQueue;
	m_graphicsFamily = graphicsFamily;
	m_presentFamily = presentFamily;
	(void)name; // TODO: VK_EXT_debug_utils — vkSetDebugUtilsObjectNameEXT for queue handles
	return true;
}


bool CommandQueue::InitSyncObjects(VkSwapchainKHR swapchain)
noexcept
{
	if (m_device == VK_NULL_HANDLE) {
		logHandler.Print("CommandQueue::InitSyncObjects: device not set, call Create first\n");
		return false;
	}
	if (swapchain == VK_NULL_HANDLE) {
		logHandler.Print("CommandQueue::InitSyncObjects: null swapchain\n");
		return false;
	}
	m_swapchain = swapchain;
	return CreateSyncObjects();
}


void CommandQueue::Destroy(void)
noexcept
{
	WaitIdle();
#if VK_STALL_DIAG
	VkGpuPassDestroy(m_device);
#endif
	VkGpuFrameDestroy(m_device);
	DestroySyncObjects();
	m_swapchain = VK_NULL_HANDLE;
	m_graphicsQueue = VK_NULL_HANDLE;
	m_presentQueue = VK_NULL_HANDLE;
	m_device = VK_NULL_HANDLE;
}


bool CommandQueue::BeginFrame(bool acquireImage)
noexcept
{
	++m_frameNumber;
	m_hasImage = false;
#if VK_STALL_DIAG
	VkStallEndFrame(m_frameNumber);
	// VMA refreshes what it knows of the driver's heap budgets when the frame index changes
	// (VK_EXT_memory_budget), see VkStallPrintHeaps ().
	if (vkContext.Allocator() != VK_NULL_HANDLE)
		vmaSetCurrentFrameIndex(vkContext.Allocator(), uint32_t(m_frameNumber));
	double stallStart = VkStallClock();
#endif
	// Wait until the GPU has finished using this frame slot.
	VkResult res = vkWaitForFences(m_device, 1, &m_inFlight[m_frameIndex], VK_TRUE, UINT64_MAX);
#if VK_STALL_DIAG
	VkStallNote("frame fence wait", stallStart, nullptr);
#endif
	if (res != VK_SUCCESS) {
		logHandler.Print("CommandQueue::BeginFrame: vkWaitForFences failed (%d)\n", (int)res);
		HandleDeviceLost(res, "CommandQueue::BeginFrame");
		return false;
	}
	res = vkResetFences(m_device, 1, &m_inFlight[m_frameIndex]);
	if (res != VK_SUCCESS) {
		logHandler.Print("CommandQueue::BeginFrame: vkResetFences failed (%d)\n", (int)res);
		return false;
	}
	// The slot's resources hang on its fence alone, so they are reset before the image is acquired -
	// a frame whose acquire fails still records, and must not do so on top of the slot's last cycle.
	VkGpuFrameResolve(m_device, m_frameIndex, m_frameNumber);
#if VK_STALL_DIAG
	VkGpuPassResolve(m_device, m_frameIndex, m_frameNumber);
	stallStart = VkStallClock();
	gfxResourceHandler.Cleanup(m_frameIndex);
	VkStallNote("frame resource cleanup", stallStart, nullptr);
	stallStart = VkStallClock();
	descriptorPoolHandler.BeginFrame(m_frameIndex);
	VkStallNote("descriptor pool reset", stallStart, nullptr);
	cbvAllocator.Reset(m_frameIndex);
	if (not acquireImage)
		return false;
	stallStart = VkStallClock();
	bool acquired = AcquireNextImage();
	VkStallNote("acquire next image", stallStart, nullptr);
	return acquired;
#else
	gfxResourceHandler.Cleanup(m_frameIndex);
	descriptorPoolHandler.BeginFrame(m_frameIndex);
	cbvAllocator.Reset(m_frameIndex);
	return acquireImage and AcquireNextImage();
#endif
}


bool CommandQueue::ReacquireImage(void)
noexcept
{
	VkResult res = vkResetFences(m_device, 1, &m_inFlight[m_frameIndex]);
	if (res != VK_SUCCESS) {
		logHandler.Print("CommandQueue::ReacquireImage: vkResetFences failed (%d)\n", int(res));
		return false;
	}
	return AcquireNextImage();
}


void CommandQueue::EndFrame(void)
noexcept
{
	if (m_hasImage)
		Present();
	m_hasImage = false;
	m_frameIndex = (m_frameIndex + 1) % FRAME_COUNT;
#if VK_STALL_DIAG
	VkGpuPassAdvance(m_frameIndex);
#endif
	VkGpuFrameAdvance(m_frameIndex);
}


void CommandQueue::WaitIdle(void)
noexcept
{
	if (m_graphicsQueue == VK_NULL_HANDLE)
		return;
#if VK_STALL_DIAG
	double stallStart = VkStallClock();
#endif
	VkResult res = vkQueueWaitIdle(m_graphicsQueue);
#if VK_STALL_DIAG
	VkStallNote("queue wait idle", stallStart, nullptr);
#endif
	if (res != VK_SUCCESS) {
		logHandler.Print("CommandQueue::WaitIdle: vkQueueWaitIdle failed (%d)\n", (int)res);
		HandleDeviceLost(res, "CommandQueue::WaitIdle");
	}
}


VkCommandBuffer CommandQueue::CmdBuffer(void) const
noexcept
{
	return commandListHandler.CurrentGfxList();
}

// =================================================================================================
// CommandQueue — private helpers

bool CommandQueue::CreateSyncObjects(void)
noexcept
{
	VkSemaphoreCreateInfo semInfo{};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // first BeginFrame must not deadlock on the wait

	for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
		VkResult r1 = vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailable[i]);
		VkResult r2 = vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlight[i]);
		if ((r1 != VK_SUCCESS) or (r2 != VK_SUCCESS)) {
			logHandler.Print("CommandQueue::CreateSyncObjects: failed at slot %u (sem=%d fence=%d)\n",
							 i, (int)r1, (int)r2);
			return false;
		}
	}
	// renderFinished is per swapchain image, not per frame slot. Allocate up to the static
	// upper bound MAX_BACK_BUFFERS — extra slots beyond the swapchain's actual ImageCount are
	// harmless since the present/submit paths only ever index by m_imageIndex < ImageCount.
	for (uint32_t i = 0; i < Swapchain::MAX_BACK_BUFFERS; ++i) {
		VkResult r = vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinished[i]);
		if (r != VK_SUCCESS) {
			logHandler.Print("CommandQueue::CreateSyncObjects: vkCreateSemaphore(renderFinished[%u]) failed (%d)\n",
							 i, (int)r);
			return false;
		}
	}
	return true;
}


void CommandQueue::DestroySyncObjects(void)
noexcept
{
	if (m_device == VK_NULL_HANDLE)
		return;
	for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
		if (m_imageAvailable[i] != VK_NULL_HANDLE) {
			vkDestroySemaphore(m_device, m_imageAvailable[i], nullptr);
			m_imageAvailable[i] = VK_NULL_HANDLE;
		}
		if (m_inFlight[i] != VK_NULL_HANDLE) {
			vkDestroyFence(m_device, m_inFlight[i], nullptr);
			m_inFlight[i] = VK_NULL_HANDLE;
		}
	}
	for (uint32_t i = 0; i < Swapchain::MAX_BACK_BUFFERS; ++i) {
		if (m_renderFinished[i] != VK_NULL_HANDLE) {
			vkDestroySemaphore(m_device, m_renderFinished[i], nullptr);
			m_renderFinished[i] = VK_NULL_HANDLE;
		}
	}
	m_acquireWaitPending = false;
}


bool CommandQueue::RecreateSyncObjects(void)
noexcept
{
	if (m_device == VK_NULL_HANDLE)
		return false;
	DestroySyncObjects();
	return CreateSyncObjects();
}


bool CommandQueue::AcquireNextImage(void)
noexcept
{
	VkResult res = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
										 m_imageAvailable[m_frameIndex], VK_NULL_HANDLE,
										 &m_imageIndex);
	if (res == VK_ERROR_OUT_OF_DATE_KHR) {
		// Swapchain is stale (e.g. window resized). Caller is BaseDisplayHandler;
		// it owns the swapchain and is expected to recreate it. Phase B.
#ifdef _DEBUG
		logHandler.Print("CommandQueue::AcquireNextImage: VK_ERROR_OUT_OF_DATE_KHR\n");
#endif
		m_swapchainIsOutOfDate = true;
		return false;
	}
	if ((res != VK_SUCCESS) and (res != VK_SUBOPTIMAL_KHR)) {
		logHandler.Print("CommandQueue::AcquireNextImage: vkAcquireNextImageKHR failed (%d)\n", (int)res);
		HandleDeviceLost(res, "CommandQueue::AcquireNextImage");
		return false;
	}
	m_acquireWaitPending = true;
	m_hasImage = true;
	return true;
}


bool CommandQueue::TakeAcquireWait(VkSemaphoreSubmitInfo& waitInfo)
noexcept
{
	if (not m_acquireWaitPending)
		return false;
	m_acquireWaitPending = false;
	waitInfo = VkSemaphoreSubmitInfo{};
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.semaphore = SubmitWaitSemaphore();
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	return true;
}


void CommandQueue::Present(void)
noexcept
{
	ZoneScopedN("CmdQueue::Present");
	VkPresentInfoKHR present{};
	present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &m_renderFinished[m_imageIndex];
	present.swapchainCount = 1;
	present.pSwapchains = &m_swapchain;
	present.pImageIndices = &m_imageIndex;

#if VK_STALL_DIAG
	double stallStart = VkStallClock();
#endif
	VkResult res = vkQueuePresentKHR(m_presentQueue, &present);
#if VK_STALL_DIAG
	VkStallNote("present", stallStart, nullptr);
#endif
	if ((res == VK_ERROR_OUT_OF_DATE_KHR) or (res == VK_SUBOPTIMAL_KHR))
		m_swapchainIsOutOfDate = true;
	if ((res != VK_SUCCESS) and (res != VK_SUBOPTIMAL_KHR) and (res != VK_ERROR_OUT_OF_DATE_KHR)) {
		logHandler.Print("CommandQueue::Present: vkQueuePresentKHR failed (%d)\n", (int)res);
		HandleDeviceLost(res, "CommandQueue::Present");
	}
}

// =================================================================================================

// =================================================================================================
// CommandList - Vulkan implementation.
//
// 1:1 port of the DX12 CommandList: each CL owns FRAME_COUNT VkCommandPools and one CB per pool.
// Open() switches to the active frame's pool/CB, vkResetCommandPool then vkBeginCommandBuffer.
// Close() ends recording. Flush() submits + waits idle for one-shot (temp) lists.

#include "shader.h"
#include "vkcontext.h"
#include "gfxstates.h"
#include "gfxrenderer.h"
#include "pipeline_cache.h"
#include "rendertarget.h"
#include "base_displayhandler.h"
#include "xrhandler.h"

#include <cstdlib>

void HandleDeviceLost(VkResult res, const char* where)
noexcept
{
	if (res != VK_ERROR_DEVICE_LOST)
		return;
	logHandler.Print("%s: VK_ERROR_DEVICE_LOST - graphics device lost, terminating (build %s)\n", where, logHandler.BuildStamp());
	// The window goes first - a message box behind a fullscreen window cannot be seen or answered.
	if (SDL_Window* window = baseDisplayHandler.GetWindow())
		SDL_HideWindow(window);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Internal Error", "Graphics device lost", nullptr);
	// No exit (): static destructors and atexit handlers would walk back into Vulkan with the dead device.
	std::_Exit(1);
}

#if VK_STALL_DIAG

static constexpr uint32_t	kGpuPassSlotSize = 256;
static constexpr uint32_t	kGpuPassCycleMax = kGpuPassSlotSize / 2;
static constexpr size_t		kGpuPassNameSize = 48;
static constexpr uint32_t	kGpuMarkMax = 96;
static constexpr size_t		kGpuMarkNameSize = 48;
static constexpr uint32_t	kGpuPassQueries = 2 + kGpuMarkMax;
static constexpr uint32_t	kGpuDrawMax = 96;

struct VkGpuPassRecord {
	const CommandList*	list;
	char				name[kGpuPassNameSize];
	bool				isEnded;
	uint32_t			markCount;
	uint32_t			marksDropped;
	const Shader*		lastShader;
	VkCommandBuffer		cb;
	bool				markIsStage[kGpuMarkMax];
	char				markNames[kGpuMarkMax][kGpuMarkNameSize];
	uint32_t			markDraws[kGpuMarkMax];
	bool				markPipelineMixed[kGpuMarkMax];
	uint64_t			markPipelines[kGpuMarkMax];
	uint32_t			markFrameHashes[kGpuMarkMax];
	uint32_t			markShaderHashes[kGpuMarkMax];
	uint32_t			drawCount;
	uint32_t			drawsDropped;
	bool				isDrawOpen;
	uint8_t				drawMarks[kGpuDrawMax];
};

struct VkGpuPassSlot {
	uint32_t		first{ 0 };
	uint32_t		count{ 0 };
	uint32_t		dropped{ 0 };
	uint64_t		frameNumber{ 0 };
	uint32_t		previousFirst{ 0 };
	uint32_t		previousCount{ 0 };
	uint32_t		previousDropped{ 0 };
	uint64_t		previousFrameNumber{ 0 };
	VkGpuPassRecord	passes[kGpuPassSlotSize]{};
};

static VkQueryPool		vkGpuPassPool = VK_NULL_HANDLE;
static VkQueryPool		vkGpuDrawPool = VK_NULL_HANDLE;
static bool				vkGpuPassUnavailable = false;
static double			vkGpuPassPeriodMs = 0.0;
static VkGpuPassSlot	vkGpuPassSlots[CommandQueue::FRAME_COUNT];
static double			vkGpuPassTimes[kGpuPassCycleMax];
static uint64_t			vkGpuPassData[kGpuPassCycleMax][kGpuPassQueries * 2];
static uint64_t			vkGpuDrawData[kGpuDrawMax * 2];


static inline uint32_t VkGpuPassQuery(uint32_t slotIndex, uint32_t index)
noexcept
{
	return (slotIndex * kGpuPassSlotSize + index) * kGpuPassQueries;
}


static inline uint32_t VkGpuDrawQuery(uint32_t slotIndex, uint32_t index)
noexcept
{
	return (slotIndex * kGpuPassSlotSize + index) * kGpuDrawMax;
}


static VkGpuPassRecord* VkGpuPassFind(VkGpuPassSlot& slot, const CommandList* list, uint32_t& index)
noexcept
{
	for (uint32_t i = slot.count; i > 0; --i) {
		index = (slot.first + i - 1) % kGpuPassSlotSize;
		VkGpuPassRecord& pass = slot.passes[index];
		if ((pass.list == list) and not pass.isEnded)
			return &pass;
	}
	return nullptr;
}


static bool VkGpuPassCreate(void)
noexcept
{
	if (vkGpuPassPool != VK_NULL_HANDLE)
		return true;
	if (vkGpuPassUnavailable)
		return false;
	VkDevice			device = vkContext.Device();
	VkPhysicalDevice	physicalDevice = vkContext.PhysicalDevice();
	uint32_t			familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
	std::vector<VkQueueFamilyProperties> families(familyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
	uint32_t family = vkContext.GraphicsFamily();
	if ((family >= familyCount) or (families[family].timestampValidBits == 0)) {
		logHandler.Print("gpu pass timing: the graphics queue has no timestamps\n");
		vkGpuPassUnavailable = true;
		return false;
	}
	VkQueryPoolCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	info.queryCount = CommandQueue::FRAME_COUNT * kGpuPassSlotSize * kGpuPassQueries;
	VkResult res = vkCreateQueryPool(device, &info, nullptr, &vkGpuPassPool);
	if (res != VK_SUCCESS) {
		logHandler.Print("gpu pass timing: vkCreateQueryPool failed (%d)\n", int(res));
		vkGpuPassPool = VK_NULL_HANDLE;
		vkGpuPassUnavailable = true;
		return false;
	}
	vkGpuPassPeriodMs = double(vkContext.DeviceProps().limits.timestampPeriod) * 1e-6;
	VkPhysicalDeviceFeatures features{};
	vkGetPhysicalDeviceFeatures(physicalDevice, &features);
	if (not features.pipelineStatisticsQuery)
		logHandler.Print("gpu pass timing: the device has no pipeline statistics, fragment counts are left out\n");
	else {
		VkQueryPoolCreateInfo drawInfo{};
		drawInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		drawInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		drawInfo.queryCount = CommandQueue::FRAME_COUNT * kGpuPassSlotSize * kGpuDrawMax;
		drawInfo.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
		res = vkCreateQueryPool(device, &drawInfo, nullptr, &vkGpuDrawPool);
		if (res != VK_SUCCESS) {
			logHandler.Print("gpu pass timing: vkCreateQueryPool for the fragment counts failed (%d)\n", int(res));
			vkGpuDrawPool = VK_NULL_HANDLE;
		}
	}
	return true;
}


static void VkGpuPassBegin(const CommandList* list, const char* name, VkCommandBuffer cb, uint32_t slotIndex)
noexcept
{
	if (not VkGpuPassCreate())
		return;
	VkGpuPassSlot& slot = vkGpuPassSlots[slotIndex];
	if (slot.count >= kGpuPassCycleMax) {
		++slot.dropped;
		return;
	}
	uint32_t index = (slot.first + slot.count) % kGpuPassSlotSize;
	++slot.count;
	VkGpuPassRecord& pass = slot.passes[index];
	pass.list = list;
	pass.isEnded = false;
	pass.markCount = 0;
	pass.marksDropped = 0;
	pass.lastShader = nullptr;
	pass.cb = cb;
	pass.drawCount = 0;
	pass.drawsDropped = 0;
	pass.isDrawOpen = false;
	snprintf(pass.name, sizeof(pass.name), "%s", name);
	uint32_t query = VkGpuPassQuery(slotIndex, index);
	vkCmdResetQueryPool(cb, vkGpuPassPool, query, kGpuPassQueries);
	if (vkGpuDrawPool != VK_NULL_HANDLE)
		vkCmdResetQueryPool(cb, vkGpuDrawPool, VkGpuDrawQuery(slotIndex, index), kGpuDrawMax);
	vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkGpuPassPool, query);
}


static void VkGpuPassMark(const CommandList* list, VkCommandBuffer cb, uint32_t slotIndex, const Shader* shader)
noexcept
{
	if (vkGpuPassPool == VK_NULL_HANDLE)
		return;
	uint32_t			index = 0;
	VkGpuPassRecord*	pass = VkGpuPassFind(vkGpuPassSlots[slotIndex], list, index);
	if ((pass == nullptr) or (pass->lastShader == shader))
		return;
	pass->lastShader = shader;
	if (pass->markCount >= kGpuMarkMax) {
		++pass->marksDropped;
		return;
	}
	snprintf(pass->markNames[pass->markCount], kGpuMarkNameSize, "%s", static_cast<const char*>(shader->m_name));
	pass->markIsStage[pass->markCount] = false;
	pass->markDraws[pass->markCount] = 0;
	vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkGpuPassPool, VkGpuPassQuery(slotIndex, index) + 2 + pass->markCount);
	++pass->markCount;
}


void VkGpuPassNoteDraw(VkCommandBuffer cb, uint32_t frameHash, uint32_t shaderHash)
noexcept
{
	if (vkGpuPassPool == VK_NULL_HANDLE)
		return;
	for (uint32_t slotIndex = 0; slotIndex < CommandQueue::FRAME_COUNT; ++slotIndex) {
		VkGpuPassSlot& slot = vkGpuPassSlots[slotIndex];
		for (uint32_t i = slot.count; i > 0; --i) {
			VkGpuPassRecord& pass = slot.passes[(slot.first + i - 1) % kGpuPassSlotSize];
			if ((pass.cb != cb) or pass.isEnded)
				continue;
			if (pass.markCount == 0)
				return;
			uint32_t mark = pass.markCount - 1;
			for (uint32_t j = pass.markCount; j > 0; --j) {
				if (pass.markIsStage[j - 1]) {
					mark = j - 1;
					break;
				}
			}
			VkPipeline	pipeline = pass.list->m_activePipeline;
			uint64_t	pipelineId = 0;
			std::memcpy(&pipelineId, &pipeline, (sizeof(pipeline) < sizeof(pipelineId)) ? sizeof(pipeline) : sizeof(pipelineId));
			if (pass.markDraws[mark] == 0) {
				pass.markPipelines[mark] = pipelineId;
				pass.markPipelineMixed[mark] = false;
				pass.markFrameHashes[mark] = frameHash;
				pass.markShaderHashes[mark] = shaderHash;
			}
			else {
				if (pass.markPipelines[mark] != pipelineId)
					pass.markPipelineMixed[mark] = true;
				pass.markFrameHashes[mark] = pass.markFrameHashes[mark] * 31u + frameHash;
				pass.markShaderHashes[mark] = pass.markShaderHashes[mark] * 31u + shaderHash;
			}
			++pass.markDraws[mark];
			return;
		}
	}
}


static VkGpuPassRecord* VkGpuPassFindOpen(VkCommandBuffer cb, uint32_t& slotIndex, uint32_t& index)
noexcept
{
	for (slotIndex = 0; slotIndex < CommandQueue::FRAME_COUNT; ++slotIndex) {
		VkGpuPassSlot& slot = vkGpuPassSlots[slotIndex];
		for (uint32_t i = slot.count; i > 0; --i) {
			index = (slot.first + i - 1) % kGpuPassSlotSize;
			VkGpuPassRecord& pass = slot.passes[index];
			if ((pass.cb == cb) and not pass.isEnded)
				return &pass;
		}
	}
	return nullptr;
}


void VkGpuPassBeginDraw(VkCommandBuffer cb)
noexcept
{
	if ((vkGpuDrawPool == VK_NULL_HANDLE) or (cb == VK_NULL_HANDLE))
		return;
	uint32_t			slotIndex = 0;
	uint32_t			index = 0;
	VkGpuPassRecord*	pass = VkGpuPassFindOpen(cb, slotIndex, index);
	if ((pass == nullptr) or (pass->markCount == 0) or pass->isDrawOpen)
		return;
	if (pass->drawCount >= kGpuDrawMax) {
		++pass->drawsDropped;
		return;
	}
	uint32_t mark = pass->markCount - 1;
	for (uint32_t j = pass->markCount; j > 0; --j) {
		if (pass->markIsStage[j - 1]) {
			mark = j - 1;
			break;
		}
	}
	pass->drawMarks[pass->drawCount] = uint8_t(mark);
	vkCmdBeginQuery(cb, vkGpuDrawPool, VkGpuDrawQuery(slotIndex, index) + pass->drawCount, 0);
	pass->isDrawOpen = true;
}


void VkGpuPassEndDraw(VkCommandBuffer cb)
noexcept
{
	if ((vkGpuDrawPool == VK_NULL_HANDLE) or (cb == VK_NULL_HANDLE))
		return;
	uint32_t			slotIndex = 0;
	uint32_t			index = 0;
	VkGpuPassRecord*	pass = VkGpuPassFindOpen(cb, slotIndex, index);
	if ((pass == nullptr) or not pass->isDrawOpen)
		return;
	vkCmdEndQuery(cb, vkGpuDrawPool, VkGpuDrawQuery(slotIndex, index) + pass->drawCount);
	++pass->drawCount;
	pass->isDrawOpen = false;
}


static void VkGpuPassEnd(const CommandList* list, VkCommandBuffer cb, uint32_t slotIndex)
noexcept
{
	if (vkGpuPassPool == VK_NULL_HANDLE)
		return;
	uint32_t			index = 0;
	VkGpuPassRecord*	pass = VkGpuPassFind(vkGpuPassSlots[slotIndex], list, index);
	if (pass == nullptr)
		return;
	pass->isEnded = true;
	vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkGpuPassPool, VkGpuPassQuery(slotIndex, index) + 1);
}


static void VkGpuPassAdvance(uint32_t slotIndex)
noexcept
{
	VkGpuPassSlot& slot = vkGpuPassSlots[slotIndex];
	slot.previousFirst = slot.first;
	slot.previousCount = slot.count;
	slot.previousDropped = slot.dropped;
	slot.previousFrameNumber = slot.frameNumber;
	slot.first = (slot.first + slot.count) % kGpuPassSlotSize;
	slot.count = 0;
	slot.dropped = 0;
}


static constexpr size_t	kGpuPassTextFlush = 32768;
static std::string		vkGpuPassText;


static void VkGpuPassFlush(void)
noexcept
{
	if (vkGpuPassText.empty())
		return;
	logHandler.Print("%s", vkGpuPassText.c_str());
	vkGpuPassText.clear();
}


static void VkGpuPassLine(const char* format, ...)
noexcept
{
	char	line[256];
	va_list	args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	vkGpuPassText += line;
	if (vkGpuPassText.size() >= kGpuPassTextFlush)
		VkGpuPassFlush();
}


static const char* VkGpuPassDrawText(const VkGpuPassRecord& pass, uint32_t mark, const uint64_t* fragments, const bool* hasFragments,
									 char* text, size_t size)
noexcept
{
	text[0] = '\0';
	if (pass.markDraws[mark] == 0)
		return text;
	int length = snprintf(text, size, "  pipeline %012llX%s constants %08X/%08X draws %u",
						  static_cast<unsigned long long>(pass.markPipelines[mark]), pass.markPipelineMixed[mark] ? " mixed" : "",
						  pass.markFrameHashes[mark], pass.markShaderHashes[mark], pass.markDraws[mark]);
	if (hasFragments[mark] and (length > 0) and (size_t(length) < size))
		snprintf(text + length, size - size_t(length), " fragments %llu", static_cast<unsigned long long>(fragments[mark]));
	return text;
}


static bool VkGpuPassIsSample(const VkGpuPassSlot& slot, uint32_t first, uint32_t count, uint64_t frameNumber)
noexcept
{
	if ((frameNumber % kSampleFrameInterval) != 0)
		return false;
	for (uint32_t i = 0; i < count; ++i) {
		const VkGpuPassRecord& pass = slot.passes[(first + i) % kGpuPassSlotSize];
		for (uint32_t j = 0; j < pass.markCount; ++j) {
			if (pass.markIsStage[j])
				return true;
		}
	}
	return false;
}


static void VkGpuPassResolve(VkDevice device, uint32_t slotIndex, uint64_t frameNumber)
noexcept
{
	VkGpuPassSlot&	slot = vkGpuPassSlots[slotIndex];
	uint32_t		first = slot.previousFirst;
	uint32_t		count = slot.previousCount;
	uint32_t		dropped = slot.previousDropped;
	uint64_t		passFrame = slot.previousFrameNumber;
	slot.previousCount = 0;
	slot.previousDropped = 0;
	slot.frameNumber = frameNumber;
	if ((vkGpuPassPool == VK_NULL_HANDLE) or (count == 0))
		return;

	uint64_t	firstBegin = UINT64_MAX;
	uint64_t	lastEnd = 0;
	double		totalMs = 0.0;
	uint32_t	timed = 0;
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t	index = (first + i) % kGpuPassSlotSize;
		uint64_t*	data = vkGpuPassData[i];
		std::memset(data, 0, sizeof(vkGpuPassData[i]));
		vkGpuPassTimes[i] = -1.0;
		if (not slot.passes[index].isEnded)
			continue;
		VkResult res = vkGetQueryPoolResults(device, vkGpuPassPool, VkGpuPassQuery(slotIndex, index), kGpuPassQueries,
											 sizeof(vkGpuPassData[i]), data, 2 * sizeof(uint64_t),
											 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
		if (((res != VK_SUCCESS) and (res != VK_NOT_READY)) or (data[1] == 0) or (data[3] == 0) or (data[2] < data[0]))
			continue;
		vkGpuPassTimes[i] = double(data[2] - data[0]) * vkGpuPassPeriodMs;
		totalMs += vkGpuPassTimes[i];
		++timed;
		if (firstBegin > data[0])
			firstBegin = data[0];
		if (lastEnd < data[2])
			lastEnd = data[2];
	}
	if (timed == 0)
		return;
	double	spanMs = double(lastEnd - firstBegin) * vkGpuPassPeriodMs;
	bool	isStall = (vkStalls.avgGpuMs > 0.0) and (spanMs > kStallFrameFactor * vkStalls.avgGpuMs);
	if (not isStall)
		vkStalls.gpuStallRun = 0;
	else if (++vkStalls.gpuStallRun > kStallRunMax) {
		vkStalls.avgGpuMs = spanMs;
		vkStalls.gpuStallRun = 0;
		isStall = false;
	}
	if (isStall) {
		if (not vkStalls.hasStall or (vkStalls.lastStallFrame < passFrame)) {
			vkStalls.hasStall = true;
			vkStalls.lastStallFrame = passFrame;
		}
	}
	else
		vkStalls.avgGpuMs = (vkStalls.avgGpuMs > 0.0) ? vkStalls.avgGpuMs * 0.95 + spanMs * 0.05 : spanMs;
	if (not isStall and not VkGpuPassIsSample(slot, first, count, passFrame))
		return;
	double minMs = isStall ? kStallDetailMinMs : kSampleDetailMinMs;
	VkGpuPassLine("GPU %sframe %llu: span %.2f ms, passes %.2f ms, %u of %u passes timed, %u not recorded\n", isStall ? "" : "sample ",
				  static_cast<unsigned long long>(passFrame), spanMs, totalMs, timed, count, dropped);
	for (uint32_t i = 0; i < count; ++i) {
		if (vkGpuPassTimes[i] < minMs)
			continue;
		const VkGpuPassRecord&	pass = slot.passes[(first + i) % kGpuPassSlotSize];
		const uint64_t*			data = vkGpuPassData[i];
		uint64_t				fragments[kGpuMarkMax]{};
		bool					hasFragments[kGpuMarkMax]{};
		if ((vkGpuDrawPool != VK_NULL_HANDLE) and (pass.drawCount > 0)) {
			std::memset(vkGpuDrawData, 0, sizeof(vkGpuDrawData));
			VkResult res = vkGetQueryPoolResults(device, vkGpuDrawPool, VkGpuDrawQuery(slotIndex, (first + i) % kGpuPassSlotSize),
												 pass.drawCount, sizeof(vkGpuDrawData), vkGpuDrawData, 2 * sizeof(uint64_t),
												 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
			if ((res == VK_SUCCESS) or (res == VK_NOT_READY)) {
				for (uint32_t j = 0; j < pass.drawCount; ++j) {
					if (vkGpuDrawData[2 * j + 1] == 0)
						continue;
					fragments[pass.drawMarks[j]] += vkGpuDrawData[2 * j];
					hasFragments[pass.drawMarks[j]] = true;
				}
			}
		}
		VkGpuPassLine("      %-28s %8.2f ms\n", pass.name, vkGpuPassTimes[i]);
		uint64_t firstMark = data[2];
		for (uint32_t j = 0; j < pass.markCount; ++j) {
			const uint64_t* mark = data + (2 + j) * 2;
			if (mark[1] != 0) {
				firstMark = mark[0];
				break;
			}
		}
		if (firstMark >= data[0]) {
			double leadMs = double(firstMark - data[0]) * vkGpuPassPeriodMs;
			if (leadMs >= minMs)
				VkGpuPassLine("         %-35s %8.2f ms\n", (pass.markCount > 0) ? "before the first shader" : "no shader bound", leadMs);
		}
		bool inStage = false;
		for (uint32_t j = 0; j < pass.markCount; ++j) {
			const uint64_t*	mark = data + (2 + j) * 2;
			if (mark[1] == 0)
				continue;
			bool		isStage = pass.markIsStage[j];
			uint64_t	markEnd = data[2];
			for (uint32_t k = j + 1; k < pass.markCount; ++k) {
				const uint64_t* next = data + (2 + k) * 2;
				if ((next[1] != 0) and (pass.markIsStage[k] or not isStage)) {
					markEnd = next[0];
					break;
				}
			}
			if (markEnd < mark[0])
				continue;
			double	markMs = double(markEnd - mark[0]) * vkGpuPassPeriodMs;
			char	drawText[128];
			if (isStage) {
				inStage = true;
				if (isStall or (markMs >= minMs))
					VkGpuPassLine("         stage %-29s %8.2f ms%s\n", pass.markNames[j], markMs,
								  VkGpuPassDrawText(pass, j, fragments, hasFragments, drawText, sizeof(drawText)));
			}
			else if (markMs >= minMs)
				VkGpuPassLine("%s shader %-28s %8.2f ms%s\n", inStage ? "            " : "        ", pass.markNames[j], markMs,
							  VkGpuPassDrawText(pass, j, fragments, hasFragments, drawText, sizeof(drawText)));
		}
		if (pass.marksDropped > 0)
			VkGpuPassLine("         %u marks not recorded\n", pass.marksDropped);
		if (pass.drawsDropped > 0)
			VkGpuPassLine("         %u draws without a fragment count\n", pass.drawsDropped);
	}
	VkGpuPassFlush();
}


static void VkGpuPassDestroy(VkDevice device)
noexcept
{
	if ((vkGpuPassPool != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE))
		vkDestroyQueryPool(device, vkGpuPassPool, nullptr);
	vkGpuPassPool = VK_NULL_HANDLE;
	if ((vkGpuDrawPool != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE))
		vkDestroyQueryPool(device, vkGpuDrawPool, nullptr);
	vkGpuDrawPool = VK_NULL_HANDLE;
	for (VkGpuPassSlot& slot : vkGpuPassSlots) {
		slot.first = 0;
		slot.count = 0;
		slot.dropped = 0;
		slot.frameNumber = 0;
		slot.previousFirst = 0;
		slot.previousCount = 0;
		slot.previousDropped = 0;
		slot.previousFrameNumber = 0;
	}
}

#endif

static constexpr uint32_t	kGpuFrameSlotSize = 256;
static constexpr uint32_t	kGpuFrameCycleMax = kGpuFrameSlotSize / 2;
static constexpr uint32_t	kGpuFrameStageMax = 128;
static constexpr uint32_t	kGpuFrameViewMax = 2;
static constexpr uint32_t	kGpuFrameQueries = (2 + kGpuFrameStageMax) * kGpuFrameViewMax;
static constexpr size_t		kGpuFrameNameSize = 48;
static constexpr uint32_t	kGpuFrameEntryMax = 128;
static constexpr uint16_t	kGpuFrameOtherLists = 0;
static constexpr uint16_t	kGpuFrameOtherStages = 1;
static constexpr double		kGpuFrameMinMs = 0.1;
static constexpr double		kGpuFrameSummaryMs = 1000.0;
static constexpr size_t		kGpuFrameTextSize = 8192;

struct VkGpuFrameRecord {
	const CommandList*	list;
	VkCommandBuffer		cb;
	bool				isEnded;
	uint16_t			entry;
	uint32_t			stageCount;
	uint32_t			stagesDropped;
	uint16_t			stageEntries[kGpuFrameStageMax];
};

struct VkGpuFrameSlot {
	uint32_t			first{ 0 };
	uint32_t			count{ 0 };
	uint32_t			dropped{ 0 };
	uint64_t			frameNumber{ 0 };
	uint32_t			previousFirst{ 0 };
	uint32_t			previousCount{ 0 };
	uint32_t			previousDropped{ 0 };
	uint64_t			previousFrameNumber{ 0 };
	VkGpuFrameRecord	records[kGpuFrameSlotSize]{};
};

struct VkGpuFrameEntry {
	char	name[kGpuFrameNameSize];
	bool	isStage;
	double	frameMs;
	double	totalMs;
	double	maxMs;
};

struct VkGpuFrameSummary {
	double			startMs{ 0.0 };
	uint32_t		frames{ 0 };
	uint32_t		overPeriod{ 0 };
	uint32_t		listsDropped{ 0 };
	uint32_t		stagesDropped{ 0 };
	double			spanTotalMs{ 0.0 };
	double			spanMaxMs{ 0.0 };
	double			busyTotalMs{ 0.0 };
	double			busyMaxMs{ 0.0 };
	double			periodMs{ 0.0 };
	uint32_t		entryCount{ 0 };
	VkGpuFrameEntry	entries[kGpuFrameEntryMax]{};
};

static bool					vkGpuFrameOn = false;
static VkQueryPool			vkGpuFramePool = VK_NULL_HANDLE;
static bool					vkGpuFrameUnavailable = false;
static double				vkGpuFrameTickMs = 0.0;
static VkGpuFrameSlot		vkGpuFrameSlots[CommandQueue::FRAME_COUNT];
static uint64_t				vkGpuFrameData[kGpuFrameQueries * 2];
static VkGpuFrameSummary	vkGpuFrameSummary;
static char					vkGpuFrameText[kGpuFrameTextSize];


static double VkGpuFrameClock(void)
noexcept
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


static inline uint32_t VkGpuFrameQuery(uint32_t slotIndex, uint32_t index)
noexcept
{
	return (slotIndex * kGpuFrameSlotSize + index) * kGpuFrameQueries;
}


static inline const uint64_t* VkGpuFrameStamp(uint32_t stamp)
noexcept
{
	return vkGpuFrameData + stamp * kGpuFrameViewMax * 2;
}


static void VkGpuFrameSetEntry(VkGpuFrameEntry& entry, const char* name, bool isStage)
noexcept
{
	snprintf(entry.name, sizeof(entry.name), "%s", name);
	entry.isStage = isStage;
	entry.frameMs = 0.0;
	entry.totalMs = 0.0;
	entry.maxMs = 0.0;
}


static uint16_t VkGpuFrameEntryIndex(const char* name, bool isStage)
noexcept
{
	VkGpuFrameSummary&	summary = vkGpuFrameSummary;
	char				key[kGpuFrameNameSize];
	size_t				length = 0;

	if (summary.entryCount == 0) {
		VkGpuFrameSetEntry(summary.entries[kGpuFrameOtherLists], "(other lists)", false);
		VkGpuFrameSetEntry(summary.entries[kGpuFrameOtherStages], "(other stages)", true);
		summary.entryCount = 2;
	}
	while ((name[length] != '\0') and (length < sizeof(key) - 1) and ((name[length] < '0') or (name[length] > '9'))) {
		key[length] = name[length];
		++length;
	}
	while ((length > 0) and ((key[length - 1] == ' ') or (key[length - 1] == ':')))
		--length;
	key[length] = '\0';
	if (length == 0)
		snprintf(key, sizeof(key), "%s", name);
	for (uint32_t i = 2; i < summary.entryCount; ++i) {
		if ((summary.entries[i].isStage == isStage) and (std::strcmp(summary.entries[i].name, key) == 0))
			return uint16_t(i);
	}
	if (summary.entryCount >= kGpuFrameEntryMax)
		return isStage ? kGpuFrameOtherStages : kGpuFrameOtherLists;
	VkGpuFrameSetEntry(summary.entries[summary.entryCount], key, isStage);
	return uint16_t(summary.entryCount++);
}


static VkGpuFrameRecord* VkGpuFrameFind(VkGpuFrameSlot& slot, const CommandList* list, uint32_t& index)
noexcept
{
	for (uint32_t i = slot.count; i > 0; --i) {
		index = (slot.first + i - 1) % kGpuFrameSlotSize;
		VkGpuFrameRecord& record = slot.records[index];
		if ((record.list == list) and not record.isEnded)
			return &record;
	}
	return nullptr;
}


static bool VkGpuFrameCreate(void)
noexcept
{
	if (vkGpuFramePool != VK_NULL_HANDLE)
		return true;
	if (vkGpuFrameUnavailable)
		return false;
	VkPhysicalDevice	physicalDevice = vkContext.PhysicalDevice();
	uint32_t			familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
	AutoArray<VkQueueFamilyProperties> families(static_cast<int32_t>(familyCount));
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.Data());
	uint32_t family = vkContext.GraphicsFamily();
	if ((family >= familyCount) or (families[int32_t(family)].timestampValidBits == 0)) {
		logHandler.Print("GPU frame timing: the graphics queue has no timestamps\n");
		vkGpuFrameUnavailable = true;
		return false;
	}
	VkQueryPoolCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	info.queryCount = CommandQueue::FRAME_COUNT * kGpuFrameSlotSize * kGpuFrameQueries;
	VkResult res = vkCreateQueryPool(vkContext.Device(), &info, nullptr, &vkGpuFramePool);
	if (res != VK_SUCCESS) {
		logHandler.Print("GPU frame timing: vkCreateQueryPool failed (%d)\n", int(res));
		vkGpuFramePool = VK_NULL_HANDLE;
		vkGpuFrameUnavailable = true;
		return false;
	}
	vkGpuFrameTickMs = double(vkContext.DeviceProps().limits.timestampPeriod) * 1e-6;
	return true;
}


static void VkGpuFrameBegin(const CommandList* list, const char* name, VkCommandBuffer cb, uint32_t slotIndex)
noexcept
{
	if (not (vkGpuFrameOn and VkGpuFrameCreate()))
		return;
	VkGpuFrameSlot& slot = vkGpuFrameSlots[slotIndex];
	if (slot.count >= kGpuFrameCycleMax) {
		++slot.dropped;
		return;
	}
	uint32_t index = (slot.first + slot.count) % kGpuFrameSlotSize;
	++slot.count;
	VkGpuFrameRecord& record = slot.records[index];
	record.list = list;
	record.cb = cb;
	record.isEnded = false;
	record.entry = VkGpuFrameEntryIndex(name, false);
	record.stageCount = 0;
	record.stagesDropped = 0;
	uint32_t query = VkGpuFrameQuery(slotIndex, index);
	vkCmdResetQueryPool(cb, vkGpuFramePool, query, kGpuFrameQueries);
	vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkGpuFramePool, query);
}


static void VkGpuFrameStage(VkCommandBuffer cb, const char* name)
noexcept
{
	if ((vkGpuFramePool == VK_NULL_HANDLE) or (cb == VK_NULL_HANDLE))
		return;
	for (uint32_t slotIndex = 0; slotIndex < CommandQueue::FRAME_COUNT; ++slotIndex) {
		VkGpuFrameSlot& slot = vkGpuFrameSlots[slotIndex];
		for (uint32_t i = slot.count; i > 0; --i) {
			uint32_t			index = (slot.first + i - 1) % kGpuFrameSlotSize;
			VkGpuFrameRecord&	record = slot.records[index];
			if ((record.cb != cb) or record.isEnded)
				continue;
			if (record.stageCount >= kGpuFrameStageMax) {
				++record.stagesDropped;
				return;
			}
			record.stageEntries[record.stageCount] = VkGpuFrameEntryIndex(name, true);
			vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkGpuFramePool,
								VkGpuFrameQuery(slotIndex, index) + (2 + record.stageCount) * kGpuFrameViewMax);
			++record.stageCount;
			return;
		}
	}
}


static void VkGpuFrameEnd(const CommandList* list, VkCommandBuffer cb, uint32_t slotIndex)
noexcept
{
	if (vkGpuFramePool == VK_NULL_HANDLE)
		return;
	uint32_t			index = 0;
	VkGpuFrameRecord*	record = VkGpuFrameFind(vkGpuFrameSlots[slotIndex], list, index);
	if (record == nullptr)
		return;
	record->isEnded = true;
	vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkGpuFramePool, VkGpuFrameQuery(slotIndex, index) + kGpuFrameViewMax);
}


static void VkGpuFrameAdvance(uint32_t slotIndex)
noexcept
{
	VkGpuFrameSlot& slot = vkGpuFrameSlots[slotIndex];
	slot.previousFirst = slot.first;
	slot.previousCount = slot.count;
	slot.previousDropped = slot.dropped;
	slot.previousFrameNumber = slot.frameNumber;
	slot.first = (slot.first + slot.count) % kGpuFrameSlotSize;
	slot.count = 0;
	slot.dropped = 0;
}


static void VkGpuFrameAppend(int& length, const char* format, ...)
noexcept
{
	if (length >= int(kGpuFrameTextSize) - 1)
		return;
	va_list	args;
	va_start(args, format);
	int written = vsnprintf(vkGpuFrameText + length, kGpuFrameTextSize - size_t(length), format, args);
	va_end(args);
	if (written <= 0)
		return;
	length += written;
	if (length > int(kGpuFrameTextSize) - 1)
		length = int(kGpuFrameTextSize) - 1;
}


static void VkGpuFrameAppendEntries(int& length, bool isStage, bool isSummary)
noexcept
{
	const VkGpuFrameSummary&	summary = vkGpuFrameSummary;
	bool						isListed[kGpuFrameEntryMax]{};
	const char*					separator = isStage ? "; stages:" : "; lists:";

	for (;;) {
		int		best = -1;
		double	bestMs = kGpuFrameMinMs;
		for (uint32_t i = 0; i < summary.entryCount; ++i) {
			const VkGpuFrameEntry&	entry = summary.entries[i];
			double					ms = isSummary ? entry.totalMs / double(summary.frames) : entry.frameMs;
			if (isListed[i] or (entry.isStage != isStage) or (ms < bestMs))
				continue;
			best = int(i);
			bestMs = ms;
		}
		if (best < 0)
			return;
		isListed[best] = true;
		if (isSummary)
			VkGpuFrameAppend(length, "%s %s %.2f/%.2f", separator, summary.entries[best].name, bestMs, summary.entries[best].maxMs);
		else
			VkGpuFrameAppend(length, "%s %s %.2f", separator, summary.entries[best].name, bestMs);
		separator = ",";
	}
}


static void VkGpuFrameLogFrame(uint64_t frameNumber, double periodMs, double busyMs, double spanMs, uint32_t timed, uint32_t count,
							   uint32_t dropped, uint32_t stagesDropped)
noexcept
{
	int length = 0;
	VkGpuFrameAppend(length, "GPU frame %llu over the display period of %.2f ms: busy %.2f ms, span %.2f ms, %u of %u lists timed, %u lists and %u stages not recorded",
					 static_cast<unsigned long long>(frameNumber), periodMs, busyMs, spanMs, timed, count, dropped, stagesDropped);
	VkGpuFrameAppendEntries(length, false, false);
	VkGpuFrameAppendEntries(length, true, false);
	logHandler.Print("%s\n", vkGpuFrameText);
}


static void VkGpuFrameLogSummary(double elapsedMs)
noexcept
{
	const VkGpuFrameSummary&	summary = vkGpuFrameSummary;
	double						frames = double(summary.frames);
	int							length = 0;
	VkGpuFrameAppend(length, "GPU %u frames in %.2f s, display period %.2f ms: busy avg %.2f max %.2f ms, span avg %.2f max %.2f ms, %u frames busy longer than the period, %u lists and %u stages not recorded",
					 summary.frames, elapsedMs * 0.001, summary.periodMs, summary.busyTotalMs / frames, summary.busyMaxMs, summary.spanTotalMs / frames,
					 summary.spanMaxMs, summary.overPeriod, summary.listsDropped, summary.stagesDropped);

	uint64_t	memoryUsage = 0;
	uint64_t	memoryBudget = 0;

	if (gfxStates.GetMemoryBudget(memoryUsage, memoryBudget))
		VkGpuFrameAppend(length, ", VRAM %llu of %llu MB", static_cast<unsigned long long>(memoryUsage >> 20),
						 static_cast<unsigned long long>(memoryBudget >> 20));
	VkGpuFrameAppendEntries(length, false, true);
	VkGpuFrameAppendEntries(length, true, true);
	logHandler.Print("%s\n", vkGpuFrameText);
}


static void VkGpuFrameResetSummary(double startMs)
noexcept
{
	VkGpuFrameSummary& summary = vkGpuFrameSummary;
	summary.startMs = startMs;
	summary.frames = 0;
	summary.overPeriod = 0;
	summary.listsDropped = 0;
	summary.stagesDropped = 0;
	summary.spanTotalMs = 0.0;
	summary.spanMaxMs = 0.0;
	summary.busyTotalMs = 0.0;
	summary.busyMaxMs = 0.0;
	for (uint32_t i = 0; i < summary.entryCount; ++i) {
		summary.entries[i].frameMs = 0.0;
		summary.entries[i].totalMs = 0.0;
		summary.entries[i].maxMs = 0.0;
	}
}


static void VkGpuFrameMeasure(VkDevice device, uint32_t slotIndex, uint32_t first, uint32_t count, uint32_t dropped, uint64_t frameNumber)
noexcept
{
	const VkGpuFrameSlot&	slot = vkGpuFrameSlots[slotIndex];
	VkGpuFrameSummary&		summary = vkGpuFrameSummary;
	uint64_t				firstBegin = UINT64_MAX;
	uint64_t				lastEnd = 0;
	double					busyMs = 0.0;
	uint32_t				timed = 0;
	uint32_t				stagesDropped = 0;

	for (uint32_t i = 0; i < count; ++i) {
		uint32_t				index = (first + i) % kGpuFrameSlotSize;
		const VkGpuFrameRecord&	record = slot.records[index];
		if (not record.isEnded)
			continue;
		std::memset(vkGpuFrameData, 0, sizeof(vkGpuFrameData));
		VkResult res = vkGetQueryPoolResults(device, vkGpuFramePool, VkGpuFrameQuery(slotIndex, index), (2 + record.stageCount) * kGpuFrameViewMax,
											 sizeof(vkGpuFrameData), vkGpuFrameData, 2 * sizeof(uint64_t),
											 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
		const uint64_t*	begin = VkGpuFrameStamp(0);
		const uint64_t*	end = VkGpuFrameStamp(1);
		if (((res != VK_SUCCESS) and (res != VK_NOT_READY)) or (begin[1] == 0) or (end[1] == 0) or (end[0] < begin[0]))
			continue;
		double listMs = double(end[0] - begin[0]) * vkGpuFrameTickMs;
		summary.entries[record.entry].frameMs += listMs;
		busyMs += listMs;
		++timed;
		if (firstBegin > begin[0])
			firstBegin = begin[0];
		if (lastEnd < end[0])
			lastEnd = end[0];
		stagesDropped += record.stagesDropped;
		for (uint32_t j = 0; j < record.stageCount; ++j) {
			const uint64_t* stage = VkGpuFrameStamp(2 + j);
			if (stage[1] == 0)
				continue;
			uint64_t stageEnd = end[0];
			for (uint32_t k = j + 1; k < record.stageCount; ++k) {
				const uint64_t* next = VkGpuFrameStamp(2 + k);
				if (next[1] != 0) {
					stageEnd = next[0];
					break;
				}
			}
			if (stageEnd >= stage[0])
				summary.entries[record.stageEntries[j]].frameMs += double(stageEnd - stage[0]) * vkGpuFrameTickMs;
		}
	}
	if (timed == 0)
		return;
	double	spanMs = double(lastEnd - firstBegin) * vkGpuFrameTickMs;
	double	periodMs = xrHandler.DisplayPeriod();
	if ((periodMs > 0.0) and (busyMs > periodMs)) {
		++summary.overPeriod;
		VkGpuFrameLogFrame(frameNumber, periodMs, busyMs, spanMs, timed, count, dropped, stagesDropped);
	}
	if (periodMs > 0.0)
		summary.periodMs = periodMs;
	++summary.frames;
	summary.listsDropped += dropped;
	summary.stagesDropped += stagesDropped;
	summary.spanTotalMs += spanMs;
	if (summary.spanMaxMs < spanMs)
		summary.spanMaxMs = spanMs;
	summary.busyTotalMs += busyMs;
	if (summary.busyMaxMs < busyMs)
		summary.busyMaxMs = busyMs;
	for (uint32_t i = 0; i < summary.entryCount; ++i) {
		VkGpuFrameEntry& entry = summary.entries[i];
		entry.totalMs += entry.frameMs;
		if (entry.maxMs < entry.frameMs)
			entry.maxMs = entry.frameMs;
		entry.frameMs = 0.0;
	}
	double now = VkGpuFrameClock();
	if (summary.startMs == 0.0)
		summary.startMs = now;
	else if (now - summary.startMs >= kGpuFrameSummaryMs) {
		VkGpuFrameLogSummary(now - summary.startMs);
		VkGpuFrameResetSummary(now);
	}
}


static void VkGpuFrameSwitch(bool isOn)
noexcept
{
	if (isOn == vkGpuFrameOn)
		return;
	vkGpuFrameOn = isOn;
	if (isOn)
		VkGpuFrameResetSummary(0.0);
	logHandler.Print("GPU frame timing %s\n", isOn ? "on" : "off");
}


static void VkGpuFrameResolve(VkDevice device, uint32_t slotIndex, uint64_t frameNumber)
noexcept
{
	VkGpuFrameSlot&	slot = vkGpuFrameSlots[slotIndex];
	uint32_t		first = slot.previousFirst;
	uint32_t		count = slot.previousCount;
	uint32_t		dropped = slot.previousDropped;
	uint64_t		recordFrame = slot.previousFrameNumber;
	slot.previousCount = 0;
	slot.previousDropped = 0;
	slot.frameNumber = frameNumber;
	if ((vkGpuFramePool != VK_NULL_HANDLE) and (count > 0))
		VkGpuFrameMeasure(device, slotIndex, first, count, dropped, recordFrame);
	VkGpuFrameSwitch(xrHandler.IsRunning());
}


static void VkGpuFrameDestroy(VkDevice device)
noexcept
{
	if ((vkGpuFramePool != VK_NULL_HANDLE) and (device != VK_NULL_HANDLE))
		vkDestroyQueryPool(device, vkGpuFramePool, nullptr);
	vkGpuFramePool = VK_NULL_HANDLE;
	for (VkGpuFrameSlot& slot : vkGpuFrameSlots) {
		slot.first = 0;
		slot.count = 0;
		slot.dropped = 0;
		slot.frameNumber = 0;
		slot.previousFirst = 0;
		slot.previousCount = 0;
		slot.previousDropped = 0;
		slot.previousFrameNumber = 0;
	}
}


void GfxStageMark(const char* name)
noexcept
{
	VkGpuFrameStage(commandListHandler.CurrentGfxList(), name);
#if VK_STALL_DIAG
	++vkStalls.stageMarks;
	if (vkGpuPassPool == VK_NULL_HANDLE)
		return;
	VkCommandBuffer cb = commandListHandler.CurrentGfxList();
	if (cb == VK_NULL_HANDLE)
		return;
	for (uint32_t slotIndex = 0; slotIndex < CommandQueue::FRAME_COUNT; ++slotIndex) {
		VkGpuPassSlot& slot = vkGpuPassSlots[slotIndex];
		for (uint32_t i = slot.count; i > 0; --i) {
			uint32_t			index = (slot.first + i - 1) % kGpuPassSlotSize;
			VkGpuPassRecord&	pass = slot.passes[index];
			if ((pass.cb != cb) or pass.isEnded)
				continue;
			if (pass.markCount >= kGpuMarkMax) {
				++pass.marksDropped;
				return;
			}
			snprintf(pass.markNames[pass.markCount], kGpuMarkNameSize, "%s", name);
			pass.markIsStage[pass.markCount] = true;
			pass.markDraws[pass.markCount] = 0;
			vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkGpuPassPool, VkGpuPassQuery(slotIndex, index) + 2 + pass.markCount);
			++pass.markCount;
			pass.lastShader = nullptr;
			return;
		}
	}
#endif
}


void GfxDumpConstants(const char* label)
noexcept
{
#if VK_STALL_DIAG
	if (not VkStallWantsConstants())
		return;
	snprintf(vkStalls.constantsLabel, sizeof(vkStalls.constantsLabel), "%s", label);
	vkStalls.wantsConstants = true;
#endif
}


bool GfxSampleFrame(uint64_t& frameNumber)
noexcept
{
	frameNumber = commandListHandler.CmdQueue().FrameNumber();
#if VK_STALL_DIAG
	return (frameNumber % kSampleFrameInterval) == 0;
#else
	return false;
#endif
}

List<RenderStates> CommandList::m_renderStateStack;


uint32_t CommandList::ActiveFrameIndex(void)
noexcept
{
	return commandListHandler.CmdQueue().FrameIndex();
}


void CommandList::PushRenderStates(void)
noexcept
{
	m_renderStateStack.Push(baseRenderer.RenderStates());
}


void CommandList::PopRenderStates(void)
noexcept
{
	if (m_renderStateStack.Length() > 0)
		baseRenderer.RenderStates() = m_renderStateStack.Pop();
}


bool CommandList::Create(const String& name, bool isTemporary)
noexcept
{
	VkDevice device = vkContext.Device();
	if (device == VK_NULL_HANDLE)
		return false;

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = vkContext.GraphicsFamily();
	// RESET_COMMAND_BUFFER_BIT: these per-frame / temporary command buffers are re-recorded every
	// frame (ONE_TIME_SUBMIT, so they become invalid after execution). Open() resets the whole pool
	// via vkResetCommandPool, but a recycled/temporary list can reach vkBeginCommandBuffer with its CB
	// not in the initial state — the bit makes that (implicit) per-buffer reset legal instead of a
	// validation error. TRANSIENT_BIT stays as the short-lived-buffers allocator hint.
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
		VkResult res = vkCreateCommandPool(device, &poolInfo, nullptr, &m_pools[i]);
		if (res != VK_SUCCESS) {
			logHandler.Print("CommandList::Create: vkCreateCommandPool[%u] failed (%d)\n", i, (int)res);
			return false;
		}
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_pools[i];
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		res = vkAllocateCommandBuffers(device, &allocInfo, &m_cmdBuffers[i]);
		if (res != VK_SUCCESS) {
			logHandler.Print("CommandList::Create: vkAllocateCommandBuffers[%u] failed (%d)\n", i, (int)res);
			return false;
		}
	}
	m_id = commandListHandler.m_cmdListId++;
	m_name = name;
	m_isTemporary = isTemporary;
	return true;
}


void CommandList::Destroy(void)
noexcept
{
	m_isRecording = false;
	VkDevice device = vkContext.Device();
	if (device == VK_NULL_HANDLE)
		return;
	for (uint32_t i = 0; i < FRAME_COUNT; ++i) {
		if (m_pools[i] != VK_NULL_HANDLE) {
			vkDestroyCommandPool(device, m_pools[i], nullptr);
			m_pools[i] = VK_NULL_HANDLE;
			m_cmdBuffers[i] = VK_NULL_HANDLE;
		}
	}
}


void CommandList::Reset(void)
noexcept
{
	m_refCounter = 1;
	m_isFlushed = false;
	m_isRecording = false;
}


bool CommandList::Open(bool saveRenderStates, bool detached)
noexcept
{
	if (m_isRecording)
		return true;
	m_isDetached = detached;
	uint32_t fi = ActiveFrameIndex();
	if ((m_pools[fi] == VK_NULL_HANDLE) or (m_cmdBuffers[fi] == VK_NULL_HANDLE))
		return false;
	VkResult res = vkResetCommandPool(vkContext.Device(), m_pools[fi], 0);
	if (res != VK_SUCCESS) {
		logHandler.Print("CommandList::Open: vkResetCommandPool failed (%d)\n", (int)res);
		return false;
	}
	VkCommandBufferBeginInfo bi{};
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	res = vkBeginCommandBuffer(m_cmdBuffers[fi], &bi);
	if (res != VK_SUCCESS) {
		logHandler.Print("CommandList::Open: vkBeginCommandBuffer failed (%d)\n", (int)res);
		return false;
	}
#if VK_STALL_DIAG
	VkGpuPassBegin(this, static_cast<const char*>(m_name), m_cmdBuffers[fi], fi);
#endif
	VkGpuFrameBegin(this, static_cast<const char*>(m_name), m_cmdBuffers[fi], fi);
	m_isRecording = true;
	m_isFlushed = false;
	m_usesBackBuffer = false;
	m_openSerial = gfxResourceHandler.NextSerial();
	m_activePipeline = VK_NULL_HANDLE;
	++m_executionCounter;
	if (detached)
		commandListHandler.Register(this);
	else
		commandListHandler.PushCmdList(this);
	// Track as open. Close() will register the CL in m_pendingLists at close-order,
	// which is what the submit sequence uses. ExecuteAll forces a Close on anything
	// still in m_openLists at frame end.
	commandListHandler.m_openLists.Push(this);
#if USE_TRACY
	m_gpuZone = new tracy::VkCtxScope(commandListHandler.m_gpuProfilerCtx,
									  uint32_t(__LINE__), __FILE__, strlen(__FILE__), __FUNCTION__, strlen(__FUNCTION__),
									  (const char*)m_name, size_t(m_name.Length()), m_cmdBuffers[fi], true);
#endif
	if (detached)
		return true;
	if (saveRenderStates)
		PushRenderStates();
	gfxStates.RestoreViewport();
#ifdef _DEBUG
	//gfxStates.CheckError();
#endif
	return true;
}


void CommandList::Close(bool restoreRenderStates)
noexcept
{
	if (not m_isRecording)
		return;
	m_isRecording = false;
#if USE_TRACY
	delete m_gpuZone;
	m_gpuZone = nullptr;
#endif
	uint32_t fi = ActiveFrameIndex();
	if (baseDisplayHandler.IsInRendering() and (baseDisplayHandler.m_backBufferCb == m_cmdBuffers[fi]))
		baseDisplayHandler.SuspendBackBuffer();
#if VK_STALL_DIAG
	VkGpuPassEnd(this, m_cmdBuffers[fi], fi);
#endif
	VkGpuFrameEnd(this, m_cmdBuffers[fi], fi);
	VkResult res = vkEndCommandBuffer(m_cmdBuffers[fi]);
	if (res != VK_SUCCESS)
		logHandler.Print("CommandList::Close: vkEndCommandBuffer failed (%d)\n", (int)res);
#ifdef _DEBUG
	gfxStates.CheckError((const char*)m_name);
#endif
	if (m_isDetached) {
		commandListHandler.Register(this);
		return;
	}
	commandListHandler.PopCmdList();
	// Register in pendingLists in close-order. ExecuteAll iterates pendingLists in
	// this order, so the CL whose Close() ran first is submitted first.
	commandListHandler.Register(this);
	if (restoreRenderStates)
		PopRenderStates();
}


void CommandList::Flush(void)
noexcept
{
	if (m_isFlushed)
		return;
	m_isFlushed = true;
#if VK_STALL_DIAG
	double stallStart = VkStallClock();
#endif
	Close();

	uint32_t					fi = ActiveFrameIndex();
	VkCommandBufferSubmitInfo	cbInfos[2]{};
	uint32_t					cbCount = 0;
	CommandList*				uploadList = commandListHandler.m_uploadList;
	if (uploadList and (uploadList != this)) {
		uploadList->m_isFlushed = true;
		uploadList->Close();
		commandListHandler.m_uploadList = nullptr;
		cbInfos[cbCount].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		cbInfos[cbCount].commandBuffer = uploadList->m_cmdBuffers[fi];
		++cbCount;
	}
	cbInfos[cbCount].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cbInfos[cbCount].commandBuffer = m_cmdBuffers[fi];
	++cbCount;

	VkSubmitInfo2 submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.commandBufferInfoCount = cbCount;
	submit.pCommandBufferInfos = cbInfos;

	VkSemaphoreSubmitInfo	waitInfo{};
	bool					usesBackBuffer = m_usesBackBuffer or (uploadList and (uploadList != this) and uploadList->m_usesBackBuffer);
	if (usesBackBuffer and commandListHandler.CmdQueue().TakeAcquireWait(waitInfo)) {
		submit.waitSemaphoreInfoCount = 1;
		submit.pWaitSemaphoreInfos = &waitInfo;
	}

	VkResult res = Vk13Api::QueueSubmit2(commandListHandler.GetQueue(), 1, &submit, VK_NULL_HANDLE);
	if (res != VK_SUCCESS) {
		logHandler.Print("CommandList::Flush: vkQueueSubmit2 failed (%d)\n", (int)res);
		HandleDeviceLost(res, "CommandList::Flush");
	}
#ifdef _DEBUG
	CheckDeviceRemoved("Flush");
#endif
	commandListHandler.CmdQueue().WaitIdle();
	DisposeResources();
#if VK_STALL_DIAG
	String name = GetName();
	VkStallNote("command list flush", stallStart, static_cast<const char*>(name));
#endif
}


void CommandList::SetBarrier(VkImage image, ImageLayoutTracker& tracker, VkImageLayout newLayout,
							 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
	if (not m_isRecording or (image == VK_NULL_HANDLE))
		return;
	tracker.TransitionTo(GfxList(), newLayout, dstStage, dstAccess);
#ifdef _DEBUG
	//gfxStates.CheckError();
#endif
}


void CommandList::SetBarrier(const VkImageMemoryBarrier2* barriers, int count)
{
	if (not m_isRecording or not barriers or (count <= 0))
		return;
	VkDependencyInfo dep{};
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = uint32_t(count);
	dep.pImageMemoryBarriers = barriers;
	Vk13Api::CmdPipelineBarrier2(GfxList(), &dep);
#ifdef _DEBUG
	//gfxStates.CheckError();
#endif
}


void CommandList::DisposeResources(void)
noexcept
{
	for (auto& fn : m_disposableResources)
		fn();
	m_disposableResources.Clear();
}


void CommandList::SetActivePipeline(VkPipeline pipeline, Shader* /*shader*/)
noexcept
{
	if (pipeline != m_activePipeline) {
		if (m_isRecording and pipeline != VK_NULL_HANDLE)
			vkCmdBindPipeline(GfxList(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		m_activePipeline = pipeline;
#ifdef _DEBUG
		//gfxStates.CheckError();
#endif
	}
}


#if OPTIMIZE_SHADER_LOADING
struct PipelineTarget {
	VkFormat colorFormats[8]{};
	uint32_t colorFormatCount{ 0 };
	VkFormat depthFormat{ VK_FORMAT_UNDEFINED };
	uint32_t viewMask{ 0 };

	bool operator==(const PipelineTarget& other) const
	noexcept = default;
};

#endif
static RenderStates	lastPipelineStates;
static CommandList*	lastPipelineList = nullptr;
static Shader*		lastPipelineShader = nullptr;
#if OPTIMIZE_SHADER_LOADING
static PipelineTarget lastPipelineTarget;


static void FillPipelineTarget(PipelineKey& key)
noexcept
{
	if (RenderTarget* rt = baseRenderer.GetActiveBuffer())
		rt->FillPipelineKey(key);
	else if (baseDisplayHandler.IsInRendering()) {
		key.colorFormats[0] = baseDisplayHandler.m_swapchain.Format();
		key.colorFormatCount = 1;
		key.depthFormat = VK_FORMAT_UNDEFINED;
	}
}


static PipelineTarget GetPipelineTarget(const PipelineKey& key)
noexcept
{
	PipelineTarget target;
	std::memcpy(target.colorFormats, key.colorFormats, sizeof(target.colorFormats));
	target.colorFormatCount = key.colorFormatCount;
	target.depthFormat = key.depthFormat;
	target.viewMask = key.viewMask;
	return target;
}
#endif

static void MaskUnwrittenAttachments(RenderStates& states, uint32_t writtenCount, uint32_t attachmentCount)
noexcept
{
	if (not states.independentBlend) {
		for (uint32_t i = 1; i < attachmentCount; ++i) {
			states.blendEnable[i] = states.blendEnable[0];
			states.blendSrcRGB[i] = states.blendSrcRGB[0];
			states.blendDstRGB[i] = states.blendDstRGB[0];
			states.blendSrcAlpha[i] = states.blendSrcAlpha[0];
			states.blendDstAlpha[i] = states.blendDstAlpha[0];
			states.blendOpRGB[i] = states.blendOpRGB[0];
			states.blendOpAlpha[i] = states.blendOpAlpha[0];
			states.colorMask[i] = states.colorMask[0];
		}
		states.independentBlend = 1;
	}
	for (uint32_t i = writtenCount; i < attachmentCount; ++i)
		states.colorMask[i] = 0;
}


static void ApplyCoverageAlpha(RenderStates& states)
noexcept
{
	using BlendFactor = GfxOperations::BlendFactor;

	for (int i = 0; i < RenderStates::kColorTargets; ++i) {
		if (not states.blendEnable[i])
			continue;
		if (states.blendDstRGB[i] == BlendFactor::InvSrcAlpha) {
			states.blendSrcAlpha[i] = BlendFactor::One;
			states.blendDstAlpha[i] = BlendFactor::InvSrcAlpha;
		}
		else if (states.blendDstRGB[i] == BlendFactor::One) {
			states.blendSrcAlpha[i] = BlendFactor::Zero;
			states.blendDstAlpha[i] = BlendFactor::One;
		}
	}
}


static void ReportUnwrittenAttachments(Shader* shader, uint32_t writtenCount, uint32_t attachmentCount)
noexcept
{
#ifdef _DEBUG
	static Shader* lastReported = nullptr;
	if (shader == lastReported)
		return;
	lastReported = shader;
	logHandler.Print(
		"CommandList::GetPipeline: shader '%s' writes %u of %u color attachments; the device has neither unused attachments nor independent blend, the other attachments are undefined after the draw\n",
		static_cast<const char*>(shader->m_name), writtenCount, attachmentCount);
#endif
}

bool ResolveDrawPipeline(CommandList* cl, Shader* shader)
noexcept
{
	if ((cl->m_activePipeline != VK_NULL_HANDLE) and (cl == lastPipelineList) and (shader == lastPipelineShader)
#if OPTIMIZE_SHADER_LOADING
		and (baseRenderer.RenderStates() == lastPipelineStates)) {
		PipelineKey key{};
		FillPipelineTarget(key);
		if (GetPipelineTarget(key) == lastPipelineTarget)
			return true;
	}
#else
		and (baseRenderer.RenderStates() == lastPipelineStates))
		return true;
#endif
	return cl->GetPipeline(shader) != VK_NULL_HANDLE;
}


#if VK_STALL_DIAG

struct VkStallUsedPipeline {
	VkPipeline	pipeline;
	PipelineKey	key;
};

static std::vector<VkStallUsedPipeline> vkStallUsedPipelines;


static void VkStallNotePipelineUse(VkPipeline pipeline, const PipelineKey& key)
noexcept
{
	for (const VkStallUsedPipeline& used : vkStallUsedPipelines) {
		if ((used.pipeline == pipeline) and (std::memcmp(&used.key, &key, sizeof(PipelineKey)) == 0))
			return;
	}
	vkStallUsedPipelines.push_back(VkStallUsedPipeline{ pipeline, key });
	char detail[256];
	snprintf(detail, sizeof(detail), "shader '%s', colors %u, blend %d, fill %d, %d pipelines in use",
			 static_cast<const char*>(key.shader->m_name), key.colorFormatCount, int(key.states.blendEnable[0]),
			 int(key.states.fillMode), int(vkStallUsedPipelines.size()));
	VkStallEvent("pipeline first use", VkStallClock(), detail);
}

#endif

VkPipeline CommandList::GetPipeline(Shader* shader)
noexcept
{
	// PipelineKey {shader, RenderStates, colour/depth formats}.
	//
	// Source of truth for colorAttachmentCount is shader->m_dataLayout.m_numRenderTargets
	// (= number of SV_Target outputs the pixel shader writes), matching DX12 PSO behaviour.
	// The render-pass scope may have more color attachments than the shader writes; the
	// VK_EXT_dynamic_rendering_unused_attachments feature (enabled in VKContext::CreateDevice)
	// makes that mismatch legal.
	//
	// Color formats come from the active render surface — the active RenderTarget when one is
	// bound (RT::FillPipelineKey), or the swapchain back buffer in DrawScreen-style passes.
	// Whichever it is, we then truncate the format list to numRenderTargets so the pipeline
	// is built with exactly the slots the shader actually writes.
	PipelineKey key{};
	key.shader = shader;
	key.states = baseRenderer.RenderStates();
#if OPTIMIZE_SHADER_LOADING
	FillPipelineTarget(key);

	const PipelineTarget target = GetPipelineTarget(key);
#else
	if (RenderTarget* rt = baseRenderer.GetActiveBuffer())
		rt->FillPipelineKey(key);
	else if (baseDisplayHandler.IsInRendering()) {
		key.colorFormats[0] = baseDisplayHandler.m_swapchain.Format();
		key.colorFormatCount = 1;
		key.depthFormat = VK_FORMAT_UNDEFINED;
	}

#endif
	const RenderStates	liveStates = key.states;
	const uint32_t		numRT = uint32_t(shader->m_dataLayout.m_numRenderTargets);
	RenderTarget*		activeTarget = baseRenderer.GetActiveBuffer();
	if (activeTarget and activeTarget->CoverageAlpha())
		ApplyCoverageAlpha(key.states);
	if (numRT < key.colorFormatCount) {
		if (vkContext.HasFeature(GfxFeature::UnusedAttachments)) {
			for (uint32_t i = numRT; i < key.colorFormatCount; ++i)
				key.colorFormats[i] = VK_FORMAT_UNDEFINED;
		}
		else if (vkContext.HasFeature(GfxFeature::IndependentBlend))
			MaskUnwrittenAttachments(key.states, numRT, key.colorFormatCount);
		else
			ReportUnwrittenAttachments(shader, numRT, key.colorFormatCount);
	}

	VkPipeline p = pipelineCache.GetOrCreate(key);
	if (p != VK_NULL_HANDLE) {
#if VK_STALL_DIAG
		VkStallNotePipelineUse(p, key);
		VkGpuPassMark(this, m_cmdBuffers[ActiveFrameIndex()], ActiveFrameIndex(), shader);
#endif
		SetActivePipeline(p, shader);
		if (m_isRecording)
			key.states.SetDynamicStates(GfxList());
		m_activeTopology = key.states.topology;
		lastPipelineStates = liveStates;
		lastPipelineList = this;
		lastPipelineShader = shader;
#if OPTIMIZE_SHADER_LOADING
		lastPipelineTarget = target;
#endif
	}
	return p;
}


bool CommandList::SetTopology(Shader* shader, MeshTopology topology)
noexcept
{
	if (m_activeTopology == uint8_t(topology))
		return true;
	baseRenderer.RenderStates().topology = uint8_t(topology);
	return GetPipeline(shader) != VK_NULL_HANDLE;
}


#ifdef _DEBUG
void CommandList::CheckDeviceRemoved(const char* context)
noexcept
{
	//gfxStates.CheckError();
}
#endif

// =================================================================================================
// CommandListHandler

#ifdef _DEBUG
bool CommandListHandler::s_logCalls = false;
#endif


bool CommandListHandler::Create(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
								uint32_t graphicsFamily, uint32_t presentFamily,
								const String& name)
noexcept
{
	if (not m_cmdQueue.Create(device, graphicsQueue, presentQueue, graphicsFamily, presentFamily, name))
		return false;
#if USE_TRACY
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	// RESET_COMMAND_BUFFER_BIT: TracyVkContext's VkCtx constructor calibrates by re-recording the
	// SAME cmdbuf three times (begin/end/submit/wait), without resetting it between begins. Begins #2
	// and #3 are implicit per-buffer resets, which need this bit (else VUID-vkBeginCommandBuffer-00050).
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = graphicsFamily;
	VkCommandPool tracyPool = VK_NULL_HANDLE;
	if (vkCreateCommandPool(device, &poolInfo, nullptr, &tracyPool) == VK_SUCCESS) {
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = tracyPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		VkCommandBuffer tracyCb = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(device, &allocInfo, &tracyCb) == VK_SUCCESS)
			m_gpuProfilerCtx = TracyVkContext(vkContext.PhysicalDevice(), device, graphicsQueue, tracyCb);
		vkDestroyCommandPool(device, tracyPool, nullptr);
	}
#endif
	return true;
}


void CommandListHandler::Destroy(void)
noexcept
{
	if (m_gpuProfilerCtx) {
		TracyVkDestroy(m_gpuProfilerCtx);
		m_gpuProfilerCtx = nullptr;
	}
	for (auto cl : m_recycledLists) {
		cl->Destroy();
		delete cl;
	}
	m_recycledLists.Clear();
	m_cmdQueue.Destroy();
}


void CommandListHandler::PushCmdList(CommandList* cl)
noexcept
{
	if (m_currentListData.cmdList)
		m_cmdListStack.Push(m_currentListData);
	m_currentListData = CommandListData{ cl, cl->GfxList() };
}


void CommandListHandler::PopCmdList(void)
noexcept
{
	m_currentListData = (m_cmdListStack.Length() > 0) ? m_cmdListStack.Pop() : CommandListData();
}


void CommandListHandler::Register(CommandList* cl)
noexcept
{
	if (not cl)
		return;
#ifdef _DEBUG
	if (cl->m_name.IsEmpty())
		logHandler.Print("CommandListHandler::Register: Unnamed command list\n");
#endif
	for (auto l : m_pendingLists)
		if (cl == l)
			return;
	m_pendingLists.Push(cl);
}


void CommandListHandler::ExecuteAll(bool intermediate)
noexcept
{
	ZoneScopedN("ExecuteAll");
	// No command buffer may be submitted with an open rendering scope, and the back buffer's belongs
	// to nobody in particular - it is opened where the draw buffer stack runs empty
	// (DrawBufferHandler::SetActiveDrawBuffers ()) and would otherwise still stand here in a setup
	// phase drain. Closing it is free when there is none.
	baseDisplayHandler.SuspendBackBuffer();
	// intermediate=false (default): frame-end submit — binds the swapchain frame-sync triplet
	//   (imageAvailable wait unless an earlier submit of this frame has taken it, renderFinished
	//   signal, inFlight fence). Must be called between a
	//   prior BeginFrame (which signaled imageAvailable via vkAcquireNextImageKHR and reset the
	//   inFlight fence) and a subsequent Present (which waits on renderFinished).
	// intermediate=true: setup-phase or mid-init drain — plain submit without renderFinished and
	//   inFlight, waiting on imageAvailable only when it carries back buffer commands,
	//   followed by vkQueueWaitIdle. Lets setup-phase CommandLists go to the GPU and finish
	//   before the first BeginFrame resets cbvAllocator / drains gfxResourceHandler.
	// Force-close any CLs still recording. Their Close() registers them at the end of
	// m_pendingLists, so they get submitted last — which matches the architectural
	// contract that the outermost (last-to-close) CL runs after all the inner ones
	// whose Close() already happened during frame rendering.
	while (m_openLists.Length() > 0) {
		CommandList* l = m_openLists.Pop();
		if (l->IsRecording())
			l->Close();
	}
	m_uploadList = nullptr;

	if (m_pendingLists.IsEmpty())
		return;

	AutoArray<VkCommandBufferSubmitInfo>	cbInfos(m_pendingLists.Length());
	int										n = 0;
	bool									usesBackBuffer = false;
	for (auto l : m_pendingLists) {
		if (l->IsFlushed())
			continue;
		VkCommandBufferSubmitInfo info{};
		info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		info.commandBuffer = l->GfxList(true);
		cbInfos[n++] = info;
		if (l->m_usesBackBuffer)
			usesBackBuffer = true;
	}
	if (n > 0) {
		VkSubmitInfo2 submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit.commandBufferInfoCount = uint32_t(n);
		submit.pCommandBufferInfos = cbInfos.Data();

		VkSemaphoreSubmitInfo	waitInfo{};
		VkSemaphoreSubmitInfo	signalInfo{};
		VkFence					fence = VK_NULL_HANDLE;
		if ((usesBackBuffer or not intermediate) and m_cmdQueue.TakeAcquireWait(waitInfo)) {
			submit.waitSemaphoreInfoCount = 1;
			submit.pWaitSemaphoreInfos = &waitInfo;
		}
		if (not intermediate) {
			if (m_cmdQueue.HasImage()) {
				signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				signalInfo.semaphore = m_cmdQueue.SubmitSignalSemaphore();
				signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

				submit.signalSemaphoreInfoCount = 1;
				submit.pSignalSemaphoreInfos = &signalInfo;
			}
			fence = m_cmdQueue.SubmitSignalFence();
		}

		{
			ZoneScopedN("vkQueueSubmit2");
#if VK_STALL_DIAG
			double stallStart = VkStallClock();
#endif
			VkResult res = Vk13Api::QueueSubmit2(m_cmdQueue.GraphicsQueue(), 1, &submit, fence);
#if VK_STALL_DIAG
			VkStallNote(intermediate ? "intermediate submit" : "frame submit", stallStart, nullptr);
#endif
			if (res != VK_SUCCESS) {
				logHandler.Print("CommandListHandler::ExecuteAll: vkQueueSubmit2 failed (%d)\n", (int)res);
				HandleDeviceLost(res, "CommandListHandler::ExecuteAll");
			}
		}
	}
#ifdef _DEBUG
	gfxStates.CheckError("CommandListHandler::ExecuteAll submit");
#endif
	for (auto l : m_pendingLists) {
		if (l->IsTemporary())
			m_recycledLists.Push(l);
	}
	m_pendingLists.Clear();
	m_cmdListStack.Clear();

	if (intermediate)
		m_cmdQueue.WaitIdle();
}


void CommandListHandler::ExecutePending(void)
noexcept
{
	ZoneScopedN("ExecutePending");
#if VK_STALL_DIAG
	double stallStart = VkStallClock();
#endif
	// The upload list is registered when it is opened, so that it runs ahead of the frame - it is the one
	// pending list that may still be recording. It goes out with the rest; the next upload opens a new one.
	if (m_uploadList) {
		m_uploadList->Close();
		m_uploadList = nullptr;
	}
	if (m_pendingLists.IsEmpty())
		return;

	AutoArray<VkCommandBufferSubmitInfo>	cbInfos(m_pendingLists.Length());
	int										n = 0;
	bool									usesBackBuffer = false;
	for (auto l : m_pendingLists) {
		if (l->IsFlushed())
			continue;
		VkCommandBufferSubmitInfo info{};
		info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
		info.commandBuffer = l->GfxList(true);
		cbInfos[n++] = info;
		if (l->m_usesBackBuffer)
			usesBackBuffer = true;
	}
	if (n > 0) {
		VkSubmitInfo2 submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
		submit.commandBufferInfoCount = uint32_t(n);
		submit.pCommandBufferInfos = cbInfos.Data();

		VkSemaphoreSubmitInfo waitInfo{};
		if (usesBackBuffer and m_cmdQueue.TakeAcquireWait(waitInfo)) {
			submit.waitSemaphoreInfoCount = 1;
			submit.pWaitSemaphoreInfos = &waitInfo;
		}

		VkResult res = Vk13Api::QueueSubmit2(m_cmdQueue.GraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
		if (res != VK_SUCCESS) {
			logHandler.Print("CommandListHandler::ExecutePending: vkQueueSubmit2 failed (%d)\n", (int)res);
			HandleDeviceLost(res, "CommandListHandler::ExecutePending");
		}
	}
#ifdef _DEBUG
	gfxStates.CheckError("CommandListHandler::ExecutePending submit");
#endif
	m_cmdQueue.WaitIdle();
#if USE_TRACY
	if (m_gpuProfilerCtx)
		TracyVkCollectHost(m_gpuProfilerCtx);
#endif
	for (auto l : m_pendingLists) {
		if (l->IsTemporary())
			m_recycledLists.Push(l);
	}
	m_pendingLists.Clear();
	DrainFrameResources();
#if VK_STALL_DIAG
	VkStallNote("execute pending", stallStart, nullptr);
#endif
}


void CommandListHandler::DrainFrameResources(void)
noexcept
{
	AutoArray<CommandList*>	recordingLists;
	uint64_t				oldestOpenSerial = UINT64_MAX;

	for (auto l : m_openLists) {
		if (not l->IsRecording())
			continue;
		recordingLists.Push(l);
		if (l->m_openSerial < oldestOpenSerial)
			oldestOpenSerial = l->m_openSerial;
	}
	m_openLists.Clear();
	for (auto l : recordingLists)
		m_openLists.Push(l);

	uint32_t frameIndex = m_cmdQueue.FrameIndex();

	gfxResourceHandler.CleanupBefore(frameIndex, oldestOpenSerial);
	if (gfxResourceHandler.LastAllocSerial() < oldestOpenSerial) {
		descriptorPoolHandler.BeginFrame(frameIndex);
		cbvAllocator.Reset(frameIndex);
	}
	ResetBindings();
}


CommandList* CommandListHandler::CreateCmdList(const String& name, bool isTemporary)
noexcept
{
	if (isTemporary and not m_recycledLists.IsEmpty()) {
		CommandList* cl = m_recycledLists.Pop();
		cl->SetName(name);
		cl->Reset();
		return cl;
	}
	CommandList* cl = new CommandList();
	if (not cl->Create(name, isTemporary)) {
		delete cl;
		return nullptr;
	}
	cl->Reset();
	++m_cmdListCount;
	return cl;
}


bool CommandListHandler::UsesOrderedCopyList(void)
noexcept
{
	return baseRenderer.DrawBuffersSuspended();
}


CommandList* CommandListHandler::OpenOrderedCopyList(void)
noexcept
{
	if (not UsesOrderedCopyList())
		return nullptr;
	CommandList* cl = CreateCmdList(String("OrderedCopy"), true);
	if (not (cl and cl->Open(false)))
		return nullptr;
	return cl;
}


bool CommandListHandler::IsInRendering(void)
noexcept
{
	VkCommandBuffer cb = CurrentGfxList();
	if (cb == VK_NULL_HANDLE)
		return false;
	if (baseDisplayHandler.IsInRendering() and (baseDisplayHandler.m_backBufferCb == cb))
		return true;
	RenderTarget* rt = baseRenderer.GetActiveBuffer();
	return rt and rt->m_isInRendering and rt->m_cmdList and (rt->m_cmdList->GfxList() == cb);
}


CommandListHandler::RenderingScope CommandListHandler::SuspendRendering(void)
noexcept
{
	RenderingScope	scope;
	VkCommandBuffer	cb = CurrentGfxList();
	if (cb == VK_NULL_HANDLE)
		return scope;
	if (baseDisplayHandler.IsInRendering() and (baseDisplayHandler.m_backBufferCb == cb)) {
		baseDisplayHandler.SuspendBackBuffer();
		scope.backBuffer = true;
		return scope;
	}
	RenderTarget* rt = baseRenderer.GetActiveBuffer();
	if (rt and rt->m_isInRendering and rt->m_cmdList and (rt->m_cmdList->GfxList() == cb)) {
		rt->EndRendering();
		scope.target = rt;
	}
	return scope;
}


void CommandListHandler::ResumeRendering(const RenderingScope& scope)
noexcept
{
	if (scope.backBuffer)
		baseDisplayHandler.EnableBackBuffer();
	else if (scope.target)
		scope.target->BeginRendering(false, false);
}


VkCommandBuffer CommandListHandler::UploadCmdBuffer(void)
noexcept
{
	if (not m_uploadList) {
		CommandList* cl = CreateCmdList(String("Upload"), true);
		if (not cl)
			return VK_NULL_HANDLE;
		if (not cl->Open(false, true)) {
			m_recycledLists.Push(cl);
			return VK_NULL_HANDLE;
		}
		m_uploadList = cl;
	}
	return m_uploadList->GfxList();
}

// =================================================================================================
// =================================================================================================
// Bind-table state (CPU-side staging of per-draw shader resource bindings).

void CommandListHandler::ResetBindings(void)
noexcept
{
	for (uint32_t i = 0; i < kSrvSlots; ++i) {
		m_boundSrvViews[i] = VK_NULL_HANDLE;
		m_boundSrvArrayViews[i] = VK_NULL_HANDLE;
		m_boundSrvLayouts[i] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	for (uint32_t i = 0; i < kSamplerSlots; ++i)
		m_boundSamplers[i] = VK_NULL_HANDLE;
	for (uint32_t i = 0; i < kUavSlots; ++i) {
		m_boundStorageBuffers[i] = VK_NULL_HANDLE;
		m_boundStorageBufferSize[i] = 0;
	}
	for (uint32_t i = 0; i < kSsboSlots; ++i) {
		m_boundReadOnlyBuffers[i] = VK_NULL_HANDLE;
		m_boundReadOnlyBufferSize[i] = 0;
	}
	for (uint32_t i = 0; i < kVertexSlots; ++i)
		m_boundInstanceStreams[i] = VK_NULL_HANDLE;
	m_boundAccelStructure = VK_NULL_HANDLE;
}


void CommandListHandler::BindAccelerationStructure(VkAccelerationStructureKHR accelStructure)
noexcept
{
	m_boundAccelStructure = accelStructure;
}


void CommandListHandler::BindSampledImage(uint32_t slot, VkImageView view, VkImageLayout layout, VkImageView arrayView)
noexcept
{
	if (slot < kSrvSlots) {
		m_boundSrvViews[slot] = view;
		m_boundSrvArrayViews[slot] = arrayView;
		m_boundSrvLayouts[slot] = layout;
	}
}


void CommandListHandler::BindSampler(uint32_t slot, VkSampler sampler)
noexcept
{
	if (slot < kSamplerSlots)
		m_boundSamplers[slot] = sampler;
}


void CommandListHandler::BindStorageBuffer(uint32_t slot, VkBuffer buffer, VkDeviceSize range)
noexcept
{
	if (slot < kUavSlots) {
		m_boundStorageBuffers[slot] = buffer;
		m_boundStorageBufferSize[slot] = range;
	}
}


void CommandListHandler::BindReadOnlyBuffer(uint32_t slot, VkBuffer buffer, VkDeviceSize range)
noexcept
{
	if (slot < kSsboSlots) {
		m_boundReadOnlyBuffers[slot] = buffer;
		m_boundReadOnlyBufferSize[slot] = range;
	}
}


void CommandListHandler::BindInstanceStream(uint32_t slot, VkBuffer buffer)
noexcept
{
	if (slot < kVertexSlots)
		m_boundInstanceStreams[slot] = buffer;
}


void CommandListHandler::UnbindBuffer(VkBuffer buffer)
noexcept
{
	if (buffer == VK_NULL_HANDLE)
		return;
	for (uint32_t i = 0; i < kUavSlots; ++i)
		if (m_boundStorageBuffers[i] == buffer)
			BindStorageBuffer(i, VK_NULL_HANDLE, 0);
	for (uint32_t i = 0; i < kSsboSlots; ++i)
		if (m_boundReadOnlyBuffers[i] == buffer)
			BindReadOnlyBuffer(i, VK_NULL_HANDLE, 0);
	for (uint32_t i = 0; i < kVertexSlots; ++i)
		if (m_boundInstanceStreams[i] == buffer)
			BindInstanceStream(i, VK_NULL_HANDLE);
}


void CommandListHandler::UnbindImage(VkImageView view)
noexcept
{
	if (view == VK_NULL_HANDLE)
		return;
	for (uint32_t i = 0; i < kSrvSlots; ++i)
		if ((m_boundSrvViews[i] == view) or (m_boundSrvArrayViews[i] == view))
			BindSampledImage(i, VK_NULL_HANDLE);
}

// =================================================================================================