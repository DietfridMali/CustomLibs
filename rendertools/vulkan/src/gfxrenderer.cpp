#define NOMINMAX

#include <stdlib.h>
#include <algorithm>
#include <utility>

#include "conversions.hpp"
#include "tristate.h"
#include "gfxrenderer.h"
#include "base_shaderhandler.h"
#include "shadowmap.h"
#include "commandlist.h"
#include "meshhandler.h"
#include "base_displayhandler.h"
#include "vkcontext.h"
#include "shader_compiler.h"
#include "pipeline_cache.h"
#include "descriptor_pool_handler.h"
#include "bindless_texture_heap.h"
#include "cbv_allocator.h"
#include "resource_handler.h"
#include "gfxapitype.h"
#include "image_layout_tracker.h"
#include "xrhandler.h"
#include "vkupload.h" // CreateReadbackBuffer for ReadBuffer ()
#include "loghandler.h"
#include "sampler_cache.h"
#include "gfxdatalayout.h"
#include "gpuregistry.h"

// =================================================================================================
// Vulkan Renderer

bool GfxRenderer::InitGraphics(const GfxFeatureRequest& request)
{
#ifdef ENABLE_VK_LOGGING
	constexpr bool enableValidation = true;
#else
	constexpr bool enableValidation = false;
#endif

	SDL_Window* window = baseDisplayHandler.GetWindow();
	if (not window) {
		logHandler.Print("GfxRenderer::InitGraphics: SDL window not yet created. Vulkan path expects window-first init order\n");
		return false;
	}

	if (not vkContext.Create(window, enableValidation, request)) {
		logHandler.Print("Smiley-Battle: Cannot create Vulkan context.\n");
		return false;
	}
	if (not ApplyFeatures(request, vkContext.Features()))
		return false;
	gfxStates.Init();
	if (not ShaderCompiler::Initialize()) {
		logHandler.Print("Smiley-Battle: Cannot initialize DXC shader compiler.\n");
		return false;
	}
	if (not pipelineCache.Create(vkContext.Device())) {
		logHandler.Print("Smiley-Battle: Cannot create Vulkan pipeline cache.\n");
		return false;
	}
	if (not descriptorPoolHandler.Create(vkContext.Device())) {
		logHandler.Print("Smiley-Battle: Cannot create Vulkan descriptor pools.\n");
		return false;
	}
	if (vkContext.HasFeature(GfxFeature::BindlessTextures) and not bindlessTextureHeap.Create(vkContext.Device(), vkContext.PhysicalDevice())) {
		logHandler.Print("Vulkan texture heap could not be created - bindless textures are off.\n");
		vkContext.m_features &= ~GfxFeatureBit(GfxFeature::BindlessTextures);
		m_gfxFeatures &= ~GfxFeatureBit(GfxFeature::BindlessTextures);
	}
	if (not cbvAllocator.Create()) {
		logHandler.Print("Smiley-Battle: Cannot create Vulkan UBO ring allocator.\n");
		return false;
	}
	if (not commandListHandler.Create(vkContext.Device(),
									  vkContext.GraphicsQueue(), vkContext.PresentQueue(),
									  vkContext.GraphicsFamily(), vkContext.PresentFamily(),
									  "MainQueue")) {
		logHandler.Print("Smiley-Battle: Cannot create Vulkan CommandQueue.\n");
		return false;
	}
	if (xrHandler.IsAvailable() and xrHandler.SessionEnabled())
		baseDisplayHandler.SetVSync(false);
	if (not baseDisplayHandler.SetupSwapchain()) {
		logHandler.Print("Smiley-Battle: Cannot create Vulkan swapchain.\n");
		return false;
	}
	if (xrHandler.IsAvailable() and xrHandler.SessionEnabled()) {
		if (not vkContext.HasFeature(GfxFeature::Multiview))
			logHandler.Print("The graphics device has no multiview with geometry and tessellation shaders - VR is off.\n");
		else if (not xrHandler.CreateSession(xrHandler.LayerWidth(), xrHandler.LayerHeight()))
			logHandler.Print("OpenXR session could not be created - VR is off.\n");
	}
	if (m_isRestarting)
		return true;
	// Arm the first frame slot so subsequent BeginFrame paths have a valid sync state.
	if (not commandListHandler.CmdQueue().BeginFrame()) {
		logHandler.Print("Smiley-Battle: Cannot begin first Vulkan frame.\n");
		return false;
	}
	return true;
}


void* GfxRenderer::StartOperation(String name, bool piggyback)
noexcept
{
	CommandList* cl = commandListHandler.CurrentCmdList();
	if (cl) {
		if (cl->IsTemporary())
			++(cl->m_refCounter);
		return cl;
	}
	if (piggyback and m_temporaryList) {
		++(m_temporaryList->m_refCounter);
		cl = m_temporaryList;
	}
	else {
		cl = commandListHandler.CreateCmdList(name, true);
		if (not cl)
			return nullptr;
		if (not cl->Open())
			return nullptr;
	}
	if (piggyback)
		m_temporaryList = cl;
	return cl;
}


bool GfxRenderer::FinishOperation(void* cl, bool flush)
noexcept
{
	CommandList* list = static_cast<CommandList*>(cl);
	if (not list)
		return false;
	if (not list->IsTemporary())
		return true;
#ifdef _DEBUG
	if (list->m_refCounter == 0)
		logHandler.Print("Invalid CL ref counter ('%s')\n", (const char*)list->GetName());
#endif
	if (--(list->m_refCounter) == 0) {
		if (flush)
			list->Flush();
		else
			list->Close();
		if (list == m_temporaryList)
			m_temporaryList = nullptr;
	}
	return true;
}


void GfxRenderer::DrawScreen(bool bRotate, bool bFlipVertically)
{
	if (not m_screenIsAvailable)
		return;
	m_frameCounter.Draw(true);
	Stop2DScene();
	UpdateFrameMetrics();
	if (not m_screenBuffer)
		return;

	Set2DRenderStates();

	bool isXR = xrHandler.IsRunning();

	if (isXR)
		xrHandler.RenderLayer(ScreenTarget()->GetAsTexture({}), bRotate, bFlipVertically, m_uiBuffer ? m_uiBuffer->GetWidth() : 0, m_uiBuffer ? m_uiBuffer->GetHeight() : 0);
	if (not baseDisplayHandler.CurrentBackBuffer())
		return;

	void* cl = StartOperation("DrawScreen", false);

	// Bring the swapchain back buffer to COLOR_ATTACHMENT and clear it before blitting.
	if (baseDisplayHandler.CurrentBackBuffer()) {
		baseDisplayHandler.EnableBackBuffer();
		gfxStates.ClearBackBuffer();
	}
	int backBufferWidth = baseDisplayHandler.GetWidth();
	int backBufferHeight = baseDisplayHandler.GetHeight();
	int x = 0;
	int y = 0;
	int w = backBufferWidth;
	int h = backBufferHeight;

	m_renderTexture.m_handle = m_screenBuffer->BufferHandle(0);

	Texture*	screen = ScreenTarget()->GetAsTexture({});
	bool		isMirrored = false;

	if (isXR and xrHandler.EyesRendered()) {
		xrHandler.WindowFit(kMaxRenderViews * xrHandler.EyeWidth(), xrHandler.EyeHeight(), x, y, w, h);
		for (int view = 0; view < kMaxRenderViews; ++view) {
			SetViewport(::Viewport(x + view * w / kMaxRenderViews, y, w / kMaxRenderViews, h), backBufferWidth, backBufferHeight);
			if (not xrHandler.RenderMirror(bFlipVertically, view))
				continue;
			isMirrored = true;
			gfxStates.SetBlending(1);
			gfxStates.BlendFunc(GfxOperations::BlendFactor::One, GfxOperations::BlendFactor::InvSrcAlpha);
			if (not xrHandler.RenderLayerMirror(screen, bFlipVertically, view))
				RenderToViewport(screen, ColorData::White, bRotate, bFlipVertically);
			Set2DRenderStates();
		}
	}
	if (not isMirrored) {
		if (xrHandler.HasSession())
			xrHandler.WindowFit(ScreenTarget()->GetWidth(), ScreenTarget()->GetHeight(), x, y, w, h);
		SetViewport(::Viewport(x, y, w, h), backBufferWidth, backBufferHeight);
		RenderToViewport(screen, ColorData::White, bRotate, bFlipVertically);
	}

	// Transition back to PRESENT_SRC_KHR for vkQueuePresentKHR.
	if (baseDisplayHandler.CurrentBackBuffer())
		baseDisplayHandler.DisableBackBuffer();
	FinishOperation(cl);
	//gfxStates.CheckError();
}


void GfxRenderer::FlushResources(void)
noexcept
{
	// Drains all pending setup-phase CommandLists and any deferred resource teardowns so that
	// the first BeginFrame can safely reset cbvAllocator and run gfxResourceHandler.Cleanup
	// without invalidating still-pending CommandBuffers. Called between Application::Setup
	// steps (analog to DX12 GfxRenderer::FlushResources → CommandListHandler::Flush).
	//
	// ExecuteAll(true) does the plain-submit-plus-WaitIdle variant (no renderFinished semaphore
	// or fence; the acquire semaphore only when back buffer commands go along). The back buffer's
	// own list is closed first, as in DX12 - it goes out with the drain and must not be taken for
	// open afterwards. Drain the deferred-cleanup lambdas for both frame slots, then clear the
	// CPU-side bind table: setup-phase Texture::Bind calls left stale handles in
	// m_boundSrvViews / m_boundSamplers / m_boundStorageBuffers; after Cleanup those handles
	// point at destroyed views/buffers, and the next render's vkUpdateDescriptorSets would
	// reject them.
	baseDisplayHandler.CloseBackBufferList();
	commandListHandler.ExecuteAll(true);
	commandListHandler.CmdQueue().WaitIdle();

	uint32_t frameIndex = commandListHandler.CmdQueue().FrameIndex();

	for (uint32_t i = 0; i < GfxResourceHandler::FRAME_COUNT; ++i) {
		if (i != frameIndex)
			gfxResourceHandler.Cleanup(i);
	}
	gfxResourceHandler.Cleanup(frameIndex);
	commandListHandler.ResetBindings();
}


void GfxRenderer::Cleanup(void)
noexcept
{
	// WaitIdle ensures the GPU is no longer using any resources. Draining both frame-slot
	// cleanup queues afterwards executes callbacks deferred via gfxResourceHandler.TrackCleanup
	// (RT BufferInfo::Release, disposable textures, ...), so the underlying VkImage / VkImageView
	// / VmaAllocation handles are destroyed before gfxResourceHandler / vkContext are torn down.
	commandListHandler.CmdQueue().WaitIdle();
	xrHandler.DestroySession();
	SavePipelineCache();
	pipelineCache.Destroy();
	GfxDataLayout::DestroyDefaultStreams();
	Shader::DestroyDefaultResources();
	meshHandler.Destroy();
	gfxResourceHandler.Cleanup(0);
	gfxResourceHandler.Cleanup(1);
	bindlessTextureHeap.Destroy();
	xrHandler.Destroy();
}


void GfxRenderer::ShutdownGraphics(void)
noexcept
{
	FlushResources();
	xrHandler.DestroySession();
	SavePipelineCache();
	pipelineCache.Destroy();
	GfxDataLayout::DestroyDefaultStreams();
	GfxDataLayout::activeLayout = nullptr;
	GfxDataLayout::layoutStack.Clear();
	Shader::DestroyDefaultResources();
	meshHandler.Destroy();
	GpuRegistry::ReleaseAll();
	baseShaderHandler.DestroyShaders();
	gfxResourceHandler.Cleanup(0);
	gfxResourceHandler.Cleanup(1);
	bindlessTextureHeap.Destroy();
	samplerCache.Destroy();
	cbvAllocator.Destroy();
	gfxResourceHandler.Cleanup(0);
	gfxResourceHandler.Cleanup(1);
	descriptorPoolHandler.Destroy();
	commandListHandler.Destroy();
	m_cmdList = nullptr;
	m_temporaryList = nullptr;
	baseDisplayHandler.DestroySwapchain();
	vkContext.Destroy();
}


bool GfxRenderer::RestartGraphics(const GfxFeatureRequest& request)
{
	ShutdownGraphics();
	m_isRestarting = true;

	bool bOk = InitGraphics(request);

	m_isRestarting = false;
	if (not bOk)
		return false;
	pipelineCache.Load();
	SetViewCount(xrHandler.HasSession() ? kMaxRenderViews : 1);
	baseShaderHandler.RecreateShaders();
	baseDisplayHandler.BeginFrame();
	GpuRegistry::RestoreAll();
	return true;
}


void GfxRenderer::LoadPipelineCache(void)
{
	pipelineCache.Load();
}


void GfxRenderer::SavePipelineCache(void)
{
	pipelineCache.Save();
}


// Precreate () replays the key records on disk, and then is the moment to WRITE the cache: a level load
// is where new records have just come together (the lazy links of the level before), the loading
// screen is up anyway, and Cleanup () - the only other save - is never reached by a run that is killed
// or crashes. Only when there is something new to write, so a run that found everything on disk does
// not write it back every time.

void GfxRenderer::PrecreatePipelines(void (*progress)(int, int))
{
	pipelineCache.Precreate(progress);
	if (pipelineCache.NeedsSave())
		SavePipelineCache();
}


void GfxRenderer::ReserveMemory(void)
{
	vkContext.ReserveMemory();
}

// =================================================================================================
// The current swap chain image, as DrawScreen () left it - so call this before the present. The copy
// goes through a host visible readback buffer on a command list of its own that is submitted with the
// closed ones (CommandListHandler::ExecutePending ()), the way RenderTarget::ReadBuffer () and the DX12
// backend do it; the image's layout is put back to what it was. Vulkan rows run
// top down and the swap chain is BGRA as a rule: the result is turned around to the bottom first RGBA
// order the OpenGL backend delivers.

bool GfxRenderer::ReadBuffer(void* buffer, size_t bufferSize, int x, int y, int width, int height)
{
	VkImage image = baseDisplayHandler.CurrentBackBuffer();

	if (not (buffer and (image != VK_NULL_HANDLE)))
		return false;

	int w = baseDisplayHandler.GetWidth();
	int h = baseDisplayHandler.GetHeight();

	if (width <= 0)
		width = w - x;
	if (height <= 0)
		height = h - y;
	if ((x < 0) or (y < 0) or (width <= 0) or (height <= 0) or (x + width > w) or (y + height > h))
		return false;

	size_t rowBytes = size_t(width) * 4;
	size_t needed = rowBytes * size_t(height);

	if (bufferSize < needed)
		return false;

	VkStagingBuffer readback;

	if (not CreateReadbackBuffer(VkDeviceSize(needed), readback))
		return false;

	baseDisplayHandler.CloseBackBufferList();

	CommandList* cl = commandListHandler.CreateCmdList(String("GfxRenderer::ReadBuffer"), true);

	if (not (cl and cl->Open(false))) {
		readback.Destroy();
		return false;
	}
	cl->m_usesBackBuffer = true;

	VkCommandBuffer		cb = cl->GfxList();
	ImageLayoutTracker&	tracker = baseDisplayHandler.CurrentBackBufferTracker();
	VkImageLayout		layoutBefore = tracker.Layout();

	tracker.ToTransferSrc(cb);

	// Vulkan rows run top down: the rectangle's bottom left (x, y) is row h - y - height from the top
	VkBufferImageCopy copy{};

	copy.bufferOffset = 0;
	copy.bufferRowLength = 0; // tightly packed
	copy.bufferImageHeight = 0;
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.mipLevel = 0;
	copy.imageSubresource.baseArrayLayer = 0;
	copy.imageSubresource.layerCount = 1;
	copy.imageOffset = { int32_t(x), int32_t(h - y - height), 0 };
	copy.imageExtent = { uint32_t(width), uint32_t(height), 1 };

	vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
	// Back to the layout the frame left it in - the present, or the next draw, expects to find it there.
	if (layoutBefore == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		tracker.ToPresent(cb);
	else if (layoutBefore == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		tracker.ToColorAttachment(cb);
	cl->Close(false);
	commandListHandler.ExecutePending();
	if (readback.mapped == nullptr) {
		readback.Destroy();
		return false;
	}

	// Host visible and coherent through VMA's AUTO mapping, so what the copy wrote is visible here.
	// Bottom row first, and BGRA swapped to RGBA where the swap chain is BGRA.
	const uint8_t*	source = static_cast<const uint8_t*>(readback.mapped);
	uint8_t*		dest = static_cast<uint8_t*>(buffer);
	VkFormat		format = baseDisplayHandler.CurrentBackBufferFormat();
	bool			bgra = (format == VK_FORMAT_B8G8R8A8_UNORM) or (format == VK_FORMAT_B8G8R8A8_SRGB);

	for (int row = 0; row < height; ++row) {
		const uint8_t*	sourceRow = source + size_t(height - 1 - row) * rowBytes;
		uint8_t*		destRow = dest + size_t(row) * rowBytes;

		if (not bgra)
			memcpy(destRow, sourceRow, rowBytes);
		else {
			for (size_t i = 0; i < rowBytes; i += 4) {
				destRow[i + 0] = sourceRow[i + 2];
				destRow[i + 1] = sourceRow[i + 1];
				destRow[i + 2] = sourceRow[i + 0];
				destRow[i + 3] = sourceRow[i + 3];
			}
		}
	}
	readback.Destroy();
	return true;
}

// =================================================================================================
