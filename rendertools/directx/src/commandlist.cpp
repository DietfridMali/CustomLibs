#include "cbv_allocator.h"
#include "dx12framework.h"
#include "dx12context.h"
#include "commandlist.h"
#include "descriptor_heap.h"
#include "bindless_texture_heap.h"
#include "resource_handler.h"
#include "gfxrenderer.h"
#include "shader.h"
#include "shader_loading.h"
#include "xrhandler.h"
#if OPTIMIZE_SHADER_LOADING
#include "base_displayhandler.h"
#endif

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <chrono>
#include "tracy_wrapper.h"
#include "loghandler.h"

// base_displayhandler.cpp - ends the program with a message when the device has been removed.
extern void HandleDeviceLost(const char* where)
noexcept;

static void DxGpuFrameBegin(const CommandList* list, const char* name, ID3D12GraphicsCommandList* gfxList, uint32_t slotIndex)
noexcept;

static void DxGpuFrameEnd(const CommandList* list, ID3D12GraphicsCommandList* gfxList, uint32_t slotIndex)
noexcept;

static void DxGpuFrameResolve(uint32_t slotIndex, uint64_t frameNumber)
noexcept;

static void DxGpuFrameAdvance(uint32_t slotIndex)
noexcept;

static void DxGpuFrameDestroy(void)
noexcept;

// CLs sind die wesentliche Datenstruktur zur Abwicklung von "Render Tasks".
// Render Tasks liegen immer zwischen open und close einer CL.Es gibt in dem Sinne keine verschachtelten Render-Tasks.
// Auch bei geschachteltem open - close von CLs wird die zuerst ausgeführt, die zuerst geschlossen wird - das liegt daran,
// dass diese zuerst in die pending-CL-Liste des CL-Handlers eingetragen wird. Dadurch wird der Vulkan-Port erleichtert.
// RenderTargets haben fixe CLs, diverse Detail-Tasks (i.d.R. Daten-Uploads) holen sich bei Bedarf eine temporäre CL.
// Temporäre CLs wandern nach Ausführung in einen Pool und werden bei Anforderung von temp.CLs bevorzugt verwendet.
// CLs verwalten auch Ressourcen, die im CL-Scope liegen, insb.PSOs für Shader.
// Temporäre CLs sind für den Fall in-Frame wiederholter Render Tasks für dasselbe Renderobjekt (i.d.R. Mesh-Datenpuffer) gedacht
// und nehmen der Objekt-Instanz die Aufgabe ab, hier eine eigene CL-Verwaltung zu implementieren, damit für jede solche Task
// auch eine CL verfügbar ist.

static_assert(CommandList::kSrvSlots == uint32_t(Shader::kSrvSlots));
static_assert(CommandList::kSamplerSlots == uint32_t(Shader::kSamplerSlots));
static_assert(CommandList::kUavSlots == uint32_t(Shader::kUavSlots));
static_assert(CommandList::kSsboSlots == uint32_t(Shader::kSsboSlots));

List<RenderStates> CommandList::m_renderStateStack;

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

// =================================================================================================
// CommandQueue

bool CommandQueue::Create(ID3D12Device* device, const String& name)
noexcept
{
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue));
	if (FAILED(hr)) {
		logHandler.Print("CommandQueue: CreateCommandQueue failed (hr=0x%08X)\n", (unsigned)hr);
		return false;
	}

	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
	if (FAILED(hr)) {
		logHandler.Print("CommandQueue: CreateFence failed (hr=0x%08X)\n", (unsigned)hr);
		return false;
	}

	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (not m_fenceEvent) {
		logHandler.Print("CommandQueue: CreateEvent failed\n");
		return false;
	}

	if (not cbvAllocator.Create(device))
		return false;
#if DBG_DIRECTX
	if (not name.IsEmpty())
		m_queue->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.Length(), (const char*)name);
#endif
	return true;
}


void CommandQueue::Destroy(void)
noexcept
{
	WaitIdle();
	DxGpuFrameDestroy();
	cbvAllocator.Destroy();
	if (m_fenceEvent) {
		CloseHandle(m_fenceEvent);
		m_fenceEvent = nullptr;
	}
	m_fence.Reset();
	m_queue.Reset();
	for (UINT i = 0; i < FRAME_COUNT; ++i)
		m_fenceValues[i] = 0;
	m_fenceCounter = 0;
}


void CommandQueue::Signal(int frameIndex)
noexcept
{
	if (not m_queue or not m_fence)
		return;
	if ((frameIndex < 0) or (frameIndex >= int(FRAME_COUNT)))
		return;
	const UINT64 fenceValue = ++m_fenceCounter;
	m_fenceValues[frameIndex] = fenceValue;
	m_queue->Signal(m_fence.Get(), fenceValue);
}


void CommandQueue::WaitForFrame(int frameIndex)
noexcept
{
	if (not m_fence or not m_fenceEvent)
		return;
	if ((frameIndex < 0) or (frameIndex >= int(FRAME_COUNT)))
		return;
	if (m_fence->GetCompletedValue() < m_fenceValues[frameIndex]) {
		HRESULT hr = m_fence->SetEventOnCompletion(m_fenceValues[frameIndex], m_fenceEvent);
		if (FAILED(hr)) {
			logHandler.Print("CommandQueue::WaitForFrame: SetEventOnCompletion failed (hr=0x%08X)\n", (unsigned)hr);
			HandleDeviceLost("CommandQueue::WaitForFrame");
			return;
		}
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
	HandleDeviceLost("CommandQueue::WaitForFrame");
}


void CommandQueue::WaitIdle(void)
noexcept
{
	if (not m_queue or not m_fence or not m_fenceEvent)
		return;
	const UINT64 value = ++m_fenceCounter;
	m_queue->Signal(m_fence.Get(), value);
	if (m_fence->GetCompletedValue() < value) {
		m_fence->SetEventOnCompletion(value, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
	HandleDeviceLost("CommandQueue::WaitIdle");
#if DBG_DIRECTX
	HRESULT removed = dx12Context.Device() ? dx12Context.Device()->GetDeviceRemovedReason() : E_FAIL;
	if (FAILED(removed)) {
		logHandler.Print("CommandQueue::WaitIdle: device removed after wait (0x%08X)\n", (unsigned)removed);
		gfxStates.CheckError();
		dx12Context.DumpDRED();
	}
#endif
}


ID3D12GraphicsCommandList* CommandQueue::List(void) const
noexcept
{
	return commandListHandler.CurrentGfxList();
}

// =================================================================================================

bool CommandList::Create(ID3D12Device* device, const String& name, bool isTemporary)
noexcept
{
	for (UINT i = 0; i < FRAME_COUNT; ++i) {
		HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i]));
		if (FAILED(hr)) {
			logHandler.Print("CommandList::Create: CreateCommandAllocator[%u] failed (hr=0x%08X)\n", i, (unsigned)hr);
			return false;
		}
	}
	HRESULT hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_gfxListPtr));
	if (FAILED(hr)) {
		logHandler.Print("CommandList::Create: CreateCommandList failed (hr=0x%08X)\n", (unsigned)hr);
		return false;
	}
	m_gfxListPtr->Close();
	m_id = commandListHandler.m_cmdListId++;
#if DBG_DIRECTX
	if (not name.IsEmpty())
		m_gfxListPtr->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.Length(), (const char*)name);
#endif
	m_name = name;
	m_isTemporary = isTemporary;
	return true;
}


void CommandList::Destroy(void)
noexcept
{
	commandListHandler.NoteStopped(this);
	m_isRecording = false;
	m_gfxListPtr.Reset();
	for (UINT i = 0; i < FRAME_COUNT; ++i)
		m_allocators[i].Reset();
}


void CommandList::Reset(void)
noexcept
{
	m_refCounter = 1;
	m_isFlushed = false;
	commandListHandler.NoteStopped(this);
	m_isRecording = false;
	m_disposableResources.Clear();
}


bool CommandList::Open(bool saveRenderStates, bool detached)
noexcept
{
	if (m_isRecording)
		return true;
	m_isDetached = detached;
	UINT frameIndex = UINT(commandListHandler.FrameIndex());
	if (not m_gfxListPtr or not m_allocators[frameIndex])
		return false;
	HRESULT hr = m_allocators[frameIndex]->Reset();
	if (FAILED(hr)) {
		logHandler.Print("CommandList::Open: allocator Reset failed (hr=0x%08X)\n", (unsigned)hr);
		return false;
	}
	hr = m_gfxListPtr->Reset(m_allocators[frameIndex].Get(), nullptr);
	if (FAILED(hr)) {
		logHandler.Print("CommandList::Open: list Reset failed (hr=0x%08X)\n", (unsigned)hr);
		return false;
	}
	DxGpuFrameBegin(this, static_cast<const char*>(m_name), m_gfxListPtr.Get(), frameIndex);
	m_isRecording = true;
	m_isFlushed = false;
	m_openSerial = gfxResourceHandler.NextSerial();
	commandListHandler.NoteRecording(this);
	m_activePSO = nullptr;
	m_activeRootSignature = nullptr;
	m_descriptorHeapsBound = false;
	ResetAppliedBindings();
	++m_executionCounter;
	if (detached)
		commandListHandler.Register(this);
	else
		commandListHandler.PushCmdList(this);
	m_openQueryCount = commandListHandler.ProfilerQueryCount();
#if USE_TRACY
	// GPU timestamp zone for this command list, named after m_name. Begin timestamp is written
	// here onto the freshly reset list; the end timestamp + ResolveQueryData follow in Close().
	m_gpuZone = new tracy::D3D12ZoneScope(commandListHandler.m_gpuProfilerCtx,
										  uint32_t(__LINE__), __FILE__, strlen(__FILE__), __FUNCTION__, strlen(__FUNCTION__),
										  (const char*)m_name, size_t(m_name.Length()), m_gfxListPtr.Get(), true);
	++commandListHandler.m_gpuZonesOpened;
#endif
	if (detached) {
		m_savedRenderStates = false;
		return true;
	}
	m_savedRenderStates = saveRenderStates;
	if (saveRenderStates)
		PushRenderStates();
	gfxStates.RestoreViewport();
#if DBG_DIRECTX
	gfxStates.CheckError();
#endif
	return true;
}


void CommandList::Close(bool restoreRenderStates)
noexcept
{
	if (not m_isRecording)
		return;
	m_isRecording = false;
	commandListHandler.NoteStopped(this);
#if USE_TRACY
	// End the GPU zone (writes the end timestamp + ResolveQueryData) while the list is still open.
	if (m_gpuZone)
		++commandListHandler.m_gpuZonesClosed;
	delete m_gpuZone;
	m_gpuZone = nullptr;
#endif
	DxGpuFrameEnd(this, m_gfxListPtr.Get(), uint32_t(commandListHandler.FrameIndex()));
	HRESULT hr = m_gfxListPtr->Close();
	if (FAILED(hr))
		logHandler.Print("CommandList::Close: list->Close() failed (hr=0x%08X)\n", (unsigned)hr);
#if DBG_DIRECTX
	gfxStates.CheckError();
#endif
	if (m_isDetached) {
		commandListHandler.Register(this);
		return;
	}
	commandListHandler.PopCmdList();
	commandListHandler.Register(this);
	if (restoreRenderStates)
		PopRenderStates();
}


void CommandList::Flush(void)
noexcept
{
	ZoneScoped;
	if (m_isFlushed)
		return;
	m_isFlushed = true;
	Close();

	ID3D12CommandList*	lists[2];
	UINT				listCount = 0;
	CommandList*		uploadList = commandListHandler.m_uploadList;
	if (uploadList and (uploadList != this)) {
		uploadList->m_isFlushed = true;
		uploadList->Close();
		commandListHandler.m_uploadList = nullptr;
		lists[listCount++] = uploadList->GfxList(true);
	}
	lists[listCount++] = GfxList(true);
	commandListHandler.GetQueue()->ExecuteCommandLists(listCount, lists);
	HandleDeviceLost("CommandList::Flush");

#if DBG_DIRECTX
	CheckDeviceRemoved("Flush");
#endif
	commandListHandler.CmdQueue().WaitIdle();
	DisposeResources();
#if 0
    // Reset allocator + list so the debug layer releases resource refs; leave closed.
    UINT fi = UINT(commandListHandler.FrameIndex());
    HRESULT hr = m_allocators[fi]->Reset();
    if (FAILED(hr))
        logHandler.Print("CommandList::Flush: allocator Reset failed (hr=0x%08X)\n", (unsigned)hr);
    hr = m_gfxListPtr->Reset(m_allocators[fi].Get(), nullptr);
    if (FAILED(hr))
        logHandler.Print("CommandList::Flush: list Reset failed (hr=0x%08X)\n", (unsigned)hr);
    else
        m_gfxListPtr->Close();
#endif
}


void CommandList::Submit(void)
noexcept
{
	ZoneScoped;
	if (m_isFlushed)
		return;
	m_isFlushed = true;
	Close();

	ID3D12CommandList* list = GfxList(true);

	commandListHandler.GetQueue()->ExecuteCommandLists(1, &list);
	HandleDeviceLost("CommandList::Submit");
#if DBG_DIRECTX
	CheckDeviceRemoved("Submit");
#endif
}


void CommandList::SetBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	if (not m_isRecording or not resource or (before == after))
		return;
	D3D12_RESOURCE_BARRIER b{};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = resource;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_gfxListPtr->ResourceBarrier(1, &b);
#if DBG_DIRECTX
	gfxStates.CheckError();
#endif
}


void CommandList::SetBarrier(D3D12_RESOURCE_BARRIER* barriers, int count)
{
	if (not m_isRecording or not barriers or (count <= 0))
		return;
	m_gfxListPtr->ResourceBarrier(UINT(count), barriers);
#if DBG_DIRECTX
	gfxStates.CheckError();
#endif
}


void CommandList::BindDescriptorHeaps(void)
noexcept
{
	if (m_descriptorHeapsBound)
		return;
	ID3D12GraphicsCommandList* list = GfxList();
	if (not list)
		return;
	ID3D12DescriptorHeap* srvHeap = descriptorHeaps.SrvHeapPtr();
	ID3D12DescriptorHeap* samplerHeap = descriptorHeaps.SamplerHeapPtr();
	if (srvHeap and samplerHeap) {
		ID3D12DescriptorHeap* heaps[] = { srvHeap, samplerHeap };
		list->SetDescriptorHeaps(2, heaps);
	}
	else if (srvHeap)
		list->SetDescriptorHeaps(1, &srvHeap);
	else
		return;
	m_descriptorHeapsBound = true;
	ResetAppliedBindings();
}


void CommandList::ResetAppliedBindings(void)
noexcept
{
	for (int i = 0; i < kTableCount; ++i)
		m_appliedTables[i] = 0;
	for (uint32_t i = 0; i < kSamplerSlots; ++i)
		m_appliedSamplers[i] = UINT32_MAX;
	m_appliedAccelStructure = 0;
	m_appliedBindless = 0;
}


void CommandList::DisposeResources(void)
noexcept
{
	for (auto& fn : m_disposableResources)
		fn();
	m_disposableResources.Clear();
}


void CommandList::SetActivePSO(ID3D12PipelineState* pso, Shader* shader)
noexcept
{
	if (pso != m_activePSO) {
		m_gfxListPtr->SetPipelineState(pso);
		ID3D12RootSignature* rootSignature = shader->GetRootSignature().Get();
		if (rootSignature != m_activeRootSignature) {
			m_gfxListPtr->SetGraphicsRootSignature(rootSignature);
			m_activeRootSignature = rootSignature;
			ResetAppliedBindings();
		}
		m_activePSO = pso;
#if DBG_DIRECTX
		gfxStates.CheckError();
#endif
	}
}


#if OPTIMIZE_SHADER_LOADING
struct PipelineTarget {
	DXGI_FORMAT	colorFormats[RenderStates::kColorTargets]{};
	uint8_t		colorFormatCount{ 0 };
	DXGI_FORMAT	depthFormat{ DXGI_FORMAT_UNKNOWN };
	uint8_t		viewCount{ 1 };

	bool operator==(const PipelineTarget& other) const
	noexcept = default;
};

#endif
static RenderStates	lastPipelineStates;
static CommandList*	lastPipelineList = nullptr;
static Shader*		lastPipelineShader = nullptr;
#if OPTIMIZE_SHADER_LOADING
static PipelineTarget lastPipelineTarget;


static void FillPipelineTarget(RenderStates& states)
noexcept
{
	if (RenderTarget* renderTarget = baseRenderer.GetActiveBuffer())
		renderTarget->FillPipelineFormats(states);
	else {
		states.colorTargetCount = 1;
		states.colorFormat = BaseDisplayHandler::BACK_BUFFER_FORMAT;
		for (auto& format : states.mrtFormats)
			format = DXGI_FORMAT_UNKNOWN;
		states.depthFormat = DXGI_FORMAT_UNKNOWN;
	}
}


static PipelineTarget GetPipelineTarget(void)
noexcept
{
	RenderStates states;
	FillPipelineTarget(states);
	PipelineTarget target;
	target.colorFormats[0] = states.colorFormat;
	for (int i = 1; i < RenderStates::kColorTargets; ++i)
		target.colorFormats[i] = states.mrtFormats[i - 1];
	target.colorFormatCount = states.colorTargetCount;
	target.depthFormat = states.depthFormat;
	target.viewCount = states.viewCount;
	return target;
}
#endif

bool ResolveDrawPipeline(CommandList* cl, Shader* shader)
noexcept
{
	if ((cl->m_activePSO != nullptr) and (cl == lastPipelineList) and (shader == lastPipelineShader)
#if OPTIMIZE_SHADER_LOADING
		and (baseRenderer.RenderStates() == lastPipelineStates) and (GetPipelineTarget() == lastPipelineTarget))
#else
		and (baseRenderer.RenderStates() == lastPipelineStates))
#endif
		return true;
	return cl->GetPSO(shader) != nullptr;
}


ID3D12PipelineState* CommandList::GetPSO(Shader* shader)
noexcept
{
	ID3D12PipelineState* pso = PSO::GetPSO(shader);
	if (pso) {
		SetActivePSO(pso, shader);
		if (m_isRecording)
			m_gfxListPtr->OMSetStencilRef(baseRenderer.RenderStates().stencilRef);
		m_activeTopology = baseRenderer.RenderStates().topology;
		lastPipelineStates = baseRenderer.RenderStates();
		lastPipelineList = this;
		lastPipelineShader = shader;
#if OPTIMIZE_SHADER_LOADING
		lastPipelineTarget = GetPipelineTarget();
#endif
	}
	return pso;
}


bool CommandList::SetTopology(Shader* shader, MeshTopology topology)
noexcept
{
	if (m_activeTopology == uint8_t(topology))
		return true;
	baseRenderer.RenderStates().topology = uint8_t(topology);
	return GetPSO(shader) != nullptr;
}


#if DBG_DIRECTX
void CommandList::CheckDeviceRemoved(const char* context)
noexcept
{
	gfxStates.CheckError();
	HRESULT removed = dx12Context.Device() ? dx12Context.Device()->GetDeviceRemovedReason() : E_FAIL;
	if (FAILED(removed)) {
		logHandler.Print("CommandList::%s: device removed (0x%08X)\n", context, (unsigned)removed);
		dx12Context.DumpDRED();
	}
}
#endif

// =================================================================================================
// CommandListHandler

#if DBG_DIRECTX
bool CommandListHandler::m_logCalls = false;
#endif

bool CommandListHandler::Create(ID3D12Device* device)
noexcept
{
	if (not m_cmdQueue.Create(device, "MainQueue"))
		return false;
	gfxResourceHandler.Init(m_frameCount);
	m_gpuProfilerCtx = TracyD3D12Context(device, m_cmdQueue.Queue());
#if USE_TRACY
	logHandler.Print("CommandListHandler::Create: Tracy GPU context %s, id %u (255 = constructor bailed out)\n",
					 m_gpuProfilerCtx ? "created" : "NOT created - no GPU zones",
					 m_gpuProfilerCtx ? unsigned(m_gpuProfilerCtx->GetId()) : 255u);
#endif
	D3D12_INDIRECT_ARGUMENT_DESC argument{};
	argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
	D3D12_COMMAND_SIGNATURE_DESC signature{};
	signature.ByteStride = UINT(sizeof(GfxDrawCommand));
	signature.NumArgumentDescs = 1;
	signature.pArgumentDescs = &argument;
	if (FAILED(device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&m_drawIndexedSignature)))) {
		logHandler.Print("CommandListHandler::Create: cannot create the command signature for indirect draws\n");
		return false;
	}
	ResetBindings();
	return true;
}


void CommandListHandler::ResetBindings(void)
noexcept
{
	for (uint32_t i = 0; i < CommandList::kSrvSlots; ++i) {
		m_boundSrvs[i] = UINT32_MAX;
		m_boundSrvArrays[i] = UINT32_MAX;
	}
	for (uint32_t i = 0; i < CommandList::kSamplerSlots; ++i)
		m_boundSamplers[i] = UINT32_MAX;
	for (uint32_t i = 0; i < CommandList::kUavSlots; ++i) {
		m_boundStorageBuffers[i] = UINT32_MAX;
		m_storageBufferStates[i] = {};
	}
	for (uint32_t i = 0; i < CommandList::kSsboSlots; ++i) {
		m_boundReadOnlyBuffers[i] = UINT32_MAX;
		m_readOnlyBufferStates[i] = {};
	}
	for (uint32_t i = 0; i < kVertexSlots; ++i)
		m_instanceStreams[i] = {};
	m_boundAccelStructure = 0;
	for (int i = 0; i < CommandList::kTableCount; ++i)
		m_bindingVersions[i] = ++m_bindingVersionCounter;
}


void CommandListHandler::BindAccelerationStructure(D3D12_GPU_VIRTUAL_ADDRESS accelStructure)
noexcept
{
	m_boundAccelStructure = accelStructure;
}


void CommandListHandler::BindSampledImage(uint32_t slot, uint32_t srvIndex, uint32_t arraySrvIndex)
noexcept
{
	if ((slot < CommandList::kSrvSlots) and ((m_boundSrvs[slot] != srvIndex) or (m_boundSrvArrays[slot] != arraySrvIndex))) {
		m_boundSrvs[slot] = srvIndex;
		m_boundSrvArrays[slot] = arraySrvIndex;
		m_bindingVersions[CommandList::kTableSrv] = ++m_bindingVersionCounter;
	}
}


void CommandListHandler::BindSampler(uint32_t slot, uint32_t samplerSlot)
noexcept
{
	if (slot < CommandList::kSamplerSlots)
		m_boundSamplers[slot] = samplerSlot;
}


void CommandListHandler::BindStorageBuffer(uint32_t slot, uint32_t uavIndex, ComPtr<ID3D12Resource>* pResource,
										   D3D12_RESOURCE_STATES* pState)
noexcept
{
	if (slot >= CommandList::kUavSlots)
		return;
	m_storageBufferStates[slot] = { pResource, pState };
	if (m_boundStorageBuffers[slot] != uavIndex) {
		m_boundStorageBuffers[slot] = uavIndex;
		m_bindingVersions[CommandList::kTableUav] = ++m_bindingVersionCounter;
	}
}


void CommandListHandler::BindReadOnlyBuffer(uint32_t slot, uint32_t srvIndex, ComPtr<ID3D12Resource>* pResource,
											D3D12_RESOURCE_STATES* pState)
noexcept
{
	if (slot >= CommandList::kSsboSlots)
		return;
	m_readOnlyBufferStates[slot] = { pResource, pState };
	if (m_boundReadOnlyBuffers[slot] != srvIndex) {
		m_boundReadOnlyBuffers[slot] = srvIndex;
		m_bindingVersions[CommandList::kTableSsbo] = ++m_bindingVersionCounter;
	}
}


void CommandListHandler::UnbindBuffer(const D3D12_RESOURCE_STATES* pState)
noexcept
{
	if (not pState)
		return;
	for (uint32_t i = 0; i < CommandList::kUavSlots; ++i) {
		if (m_storageBufferStates[i].pState == pState)
			BindStorageBuffer(i, UINT32_MAX);
	}
	for (uint32_t i = 0; i < CommandList::kSsboSlots; ++i) {
		if (m_readOnlyBufferStates[i].pState == pState)
			BindReadOnlyBuffer(i, UINT32_MAX);
	}
	for (uint32_t i = 0; i < kVertexSlots; ++i) {
		if (m_instanceStreams[i].pState == pState)
			BindInstanceStream(i);
	}
}


void CommandListHandler::BindInstanceStream(uint32_t slot, ComPtr<ID3D12Resource>* pResource, D3D12_RESOURCE_STATES* pState,
											UINT size, UINT stride)
noexcept
{
	if (slot < kVertexSlots)
		m_instanceStreams[slot] = { pResource, pState, size, stride };
}


bool CommandListHandler::InstanceStreamView(ID3D12GraphicsCommandList* list, uint32_t slot, D3D12_VERTEX_BUFFER_VIEW& view)
noexcept
{
	if (slot >= kVertexSlots)
		return false;
	const BoundInstanceStream& stream = m_instanceStreams[slot];
	if (not (list and stream.pResource and stream.pState and stream.pResource->Get()))
		return false;
	if (*stream.pState != D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) {
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = stream.pResource->Get();
		barrier.Transition.StateBefore = *stream.pState;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		list->ResourceBarrier(1, &barrier);
		*stream.pState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	}
	view.BufferLocation = stream.pResource->Get()->GetGPUVirtualAddress();
	view.SizeInBytes = stream.size;
	view.StrideInBytes = stream.stride;
	return true;
}


void CommandListHandler::UnbindImage(uint32_t srvIndex)
noexcept
{
	if (srvIndex == UINT32_MAX)
		return;
	for (uint32_t i = 0; i < CommandList::kSrvSlots; ++i) {
		if ((m_boundSrvs[i] == srvIndex) or (m_boundSrvArrays[i] == srvIndex))
			BindSampledImage(i, UINT32_MAX);
	}
}


static void TransitionBoundBuffer(ID3D12GraphicsCommandList* list, const CommandListHandler::BoundBuffer& buffer,
								  D3D12_RESOURCE_STATES after)
noexcept
{
	if (not (buffer.pResource and buffer.pState and *buffer.pResource) or (*buffer.pState == after))
		return;
	D3D12_RESOURCE_BARRIER b{};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = buffer.pResource->Get();
	b.Transition.StateBefore = *buffer.pState;
	b.Transition.StateAfter = after;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	list->ResourceBarrier(1, &b);
	*buffer.pState = after;
}


void CommandListHandler::TransitionBoundBuffers(ID3D12GraphicsCommandList* list)
noexcept
{
	static constexpr D3D12_RESOURCE_STATES kReadOnlyState =
		D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	for (uint32_t i = 0; i < CommandList::kUavSlots; ++i) {
		const BoundBuffer& buffer = m_storageBufferStates[i];
		if (buffer.pState and (*buffer.pState == kReadOnlyState))
			TransitionBoundBuffer(list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
	for (uint32_t i = 0; i < CommandList::kSsboSlots; ++i) {
		const BoundBuffer& buffer = m_readOnlyBufferStates[i];
		if (buffer.pState and (*buffer.pState != D3D12_RESOURCE_STATE_COMMON))
			TransitionBoundBuffer(list, buffer, kReadOnlyState);
	}
}


void CommandListHandler::OnDescriptorHeapChanged(void)
noexcept
{
	for (auto l : m_recordingLists)
		l->m_descriptorHeapsBound = false;
	InvalidateTables();
}


bool CommandListHandler::ApplyBindings(const Shader* shader)
noexcept
{
	CommandList*				cl = CurrentCmdList();
	ID3D12GraphicsCommandList*	list = CurrentGfxList();
	if (not (cl and list))
		return false;
	if (shader and shader->m_usesAccelStructure and (m_boundAccelStructure == 0)) {
		logHandler.Print("Shader '%s': declares an acceleration structure, but none is bound\n", (const char*)shader->m_name);
		return false;
	}
	cl->BindDescriptorHeaps();
	TransitionBoundBuffers(list);

	uint64_t srvDefaultKey = shader ? shader->m_srvDefaultKey : 0;
	uint32_t srvArrayMask = shader ? shader->m_srvArrayMask : 0;

	if ((srvDefaultKey != m_srvDefaultKey) or (srvArrayMask != m_srvArrayMask)) {
		m_srvDefaultKey = srvDefaultKey;
		m_srvArrayMask = srvArrayMask;
		m_bindingVersions[CommandList::kTableSrv] = ++m_bindingVersionCounter;
	}

	uint32_t srvs[CommandList::kSrvSlots];

	for (uint32_t i = 0; i < CommandList::kSrvSlots; ++i) {
		if (((srvArrayMask & (1u << i)) != 0) and (m_boundSrvArrays[i] != UINT32_MAX))
			srvs[i] = m_boundSrvArrays[i];
		else
			srvs[i] = (m_boundSrvs[i] != UINT32_MAX)
				? m_boundSrvs[i]
				: descriptorHeaps.DefaultSrv(shader ? shader->m_srvDefaults[i] : uint8_t(Shader::dvNone));
	}

	const UINT		tableParams[CommandList::kTableCount] = { UINT(Shader::kSrvBase), UINT(Shader::kUavBase), UINT(Shader::kSsboBase) };
	const uint32_t*	tableSlots[CommandList::kTableCount] = { srvs, m_boundStorageBuffers, m_boundReadOnlyBuffers };
	const uint32_t	tableSizes[CommandList::kTableCount] = { CommandList::kSrvSlots, CommandList::kUavSlots, CommandList::kSsboSlots };
	const uint32_t	nullIndices[CommandList::kTableCount] = { descriptorHeaps.m_nullTextureSrv, descriptorHeaps.m_nullUav,
															  descriptorHeaps.m_nullBufferSrv };

	for (bool heapChanged = true; heapChanged;) {
		heapChanged = false;
		cl->BindDescriptorHeaps();
		const uint64_t heapVersion = descriptorHeaps.HeapVersion();
		for (int t = 0; t < CommandList::kTableCount; ++t) {
			if (cl->m_appliedTables[t] == m_bindingVersions[t])
				continue;
			BuiltTable& built = m_builtTables[t];
			if ((built.version == m_bindingVersions[t]) and (built.generation == descriptorHeaps.TableGeneration()))
				gfxResourceHandler.NoteFrameAllocation();
			else {
				if (not descriptorHeaps.BuildTable(tableSlots[t], tableSizes[t], nullIndices[t], built.gpu))
					return false;
				if (heapVersion != descriptorHeaps.HeapVersion()) {
					heapChanged = true;
					break;
				}
				built.version = m_bindingVersions[t];
				built.generation = descriptorHeaps.TableGeneration();
			}
			list->SetGraphicsRootDescriptorTable(tableParams[t], built.gpu);
			cl->m_appliedTables[t] = m_bindingVersions[t];
		}
	}

	for (uint32_t i = 0; i < CommandList::kSamplerSlots; ++i) {
		uint32_t sampler = (m_boundSamplers[i] == UINT32_MAX) ? descriptorHeaps.m_defaultSampler : m_boundSamplers[i];
		if (cl->m_appliedSamplers[i] == sampler)
			continue;
		list->SetGraphicsRootDescriptorTable(UINT(Shader::kSamplerBase) + UINT(i), descriptorHeaps.m_samplerHeap.GpuHandle(sampler));
		cl->m_appliedSamplers[i] = sampler;
	}

	if (shader and shader->m_usesAccelStructure and (cl->m_appliedAccelStructure != m_boundAccelStructure)) {
		list->SetGraphicsRootShaderResourceView(UINT(Shader::kAccelBase), m_boundAccelStructure);
		cl->m_appliedAccelStructure = m_boundAccelStructure;
	}

	const uint64_t bindlessVersion = descriptorHeaps.HeapVersion() + 1;

	if (shader and shader->m_usesBindless and (cl->m_appliedBindless != bindlessVersion)) {
		for (uint32_t t = 0; t < BindlessTextureHeap::kTableCount; ++t)
			list->SetGraphicsRootDescriptorTable(UINT(Shader::kBindlessBase) + t, bindlessTextureHeap.Table(t));
		cl->m_appliedBindless = bindlessVersion;
	}
	return true;
}


bool CommandListHandler::BeginFrame(int frameIndex)
noexcept
{
	ZoneScoped;
	++m_frameNumber;
	if (frameIndex >= 0)
		SetFrameIndex(frameIndex);
	else
		NextFrame();
	DxGpuFrameAdvance(uint32_t(m_frameIndex));
	{
		ZoneScopedN("WaitForFrame");
		m_cmdQueue.WaitForFrame(m_frameIndex);
	}
	DxGpuFrameResolve(uint32_t(m_frameIndex), m_frameNumber);
	{
		ZoneScopedN("cbvAllocator::Reset");
		cbvAllocator.Reset(UINT(m_frameIndex));
		descriptorHeaps.ResetTables(uint32_t(m_frameIndex));
	}
	{
		ZoneScopedN("gfxResourceHandler::Cleanup");
		ReleaseFrameResources();
	}
	TracyD3D12Collect(m_gpuProfilerCtx);
#if DBG_DIRECTX
	dx12Context.QueryVRAM();
#endif
	return true;
}


void CommandListHandler::EndFrame(void)
noexcept
{
	ZoneScoped;
	m_cmdQueue.Signal(m_frameIndex);
	CloseProfilerQueries(false);
}


uint64_t CommandListHandler::ProfilerQueryCount(void) const
noexcept
{
#if USE_TRACY
	if (m_gpuProfilerCtx)
		return m_closedQueryCount + m_gpuProfilerCtx->QueryCounter();
#endif
	return 0;
}


void CommandListHandler::CloseProfilerQueries(bool keepRecording)
noexcept
{
#if USE_TRACY
	if (not m_gpuProfilerCtx)
		return;

	uint32_t queryCounter = m_gpuProfilerCtx->QueryCounter();
	uint64_t queryCount = m_closedQueryCount + queryCounter;
	uint64_t oldestOpenQueryCount = queryCount;

	if (keepRecording) {
		if (m_currentListData.cmdList and m_currentListData.cmdList->IsRecording() and
			(m_currentListData.cmdList->m_openQueryCount < oldestOpenQueryCount))
			oldestOpenQueryCount = m_currentListData.cmdList->m_openQueryCount;
		for (auto& data : m_cmdListStack) {
			if (data.cmdList and data.cmdList->IsRecording() and (data.cmdList->m_openQueryCount < oldestOpenQueryCount))
				oldestOpenQueryCount = data.cmdList->m_openQueryCount;
		}
	}

	uint32_t keepCount = uint32_t(queryCount - oldestOpenQueryCount);

	if (keepCount > queryCounter)
		keepCount = queryCounter;
	m_closedQueryCount += queryCounter - keepCount;
	m_gpuProfilerCtx->NewFrame(keepCount);
	{
		static uint64_t reportFrame = 0;

		if (m_frameNumber >= reportFrame) {
			reportFrame = m_frameNumber + 300;
			logHandler.Print(
				"Tracy GPU: frame %llu, zones opened %llu, closed %llu, queries %u -> %u, kept %u, connected %d, ctx id %u, collects %llu, timestamps %llu, payloads pending %llu, active %llu, completed %llu\n",
				(unsigned long long)m_frameNumber, (unsigned long long)m_gpuZonesOpened, (unsigned long long)m_gpuZonesClosed,
				queryCounter, m_gpuProfilerCtx->QueryCounter(), keepCount, tracy::GetProfiler().IsConnected() ? 1 : 0,
				unsigned(m_gpuProfilerCtx->GetId()), (unsigned long long)m_gpuProfilerCtx->CollectCalls(),
				(unsigned long long)m_gpuProfilerCtx->CollectedTimestamps(),
				(unsigned long long)m_gpuProfilerCtx->PendingPayloads(), (unsigned long long)m_gpuProfilerCtx->ActivePayload(),
				(unsigned long long)m_gpuProfilerCtx->CompletedPayload());
		}
	}
#endif
}


void CommandListHandler::Flush(void)
noexcept
{
	ZoneScoped;
	ExecuteAll();
	m_cmdQueue.WaitIdle();
	ReleaseFrameResources();
	ResetBindings();
}


void CommandListHandler::Destroy(void)
noexcept
{
	TracyD3D12Destroy(m_gpuProfilerCtx);
	m_gpuProfilerCtx = nullptr;
	for (auto cl : m_recycledLists) {
		cl->Destroy();
		delete cl;
	}
	m_recycledLists.Clear();
	m_pendingLists.Clear();
	m_cmdListStack.Clear();
	m_recordingLists.Clear();
	m_currentListData = CommandListData();
	m_uploadList = nullptr;
	m_frameIndex = 0;
	for (int i = 0; i < CommandList::kTableCount; ++i)
		m_builtTables[i] = BuiltTable();
	m_drawIndexedSignature.Reset();
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
#if DBG_DIRECTX
	if (cl->m_name.IsEmpty())
		logHandler.Print("CommandListHandler::Register: Unnamed command list\n");
#endif
	for (auto l : m_pendingLists)
		if (cl == l)
			return;
	m_pendingLists.Push(cl);
}

#define LOG_EXECUTION 0

void CommandListHandler::ExecuteAll(void)
noexcept
{
	ZoneScoped;
	while (m_recordingLists.Length() > 0) {
		CommandList* l = m_recordingLists[m_recordingLists.Length() - 1];
#if DBG_DIRECTX
		logHandler.Print("ExecuteAll: closing '%s' (open serial %llu, frame %llu), still recording at the frame end\n",
						 (const char*)l->GetName(), (unsigned long long)l->m_openSerial, (unsigned long long)m_frameNumber);
#endif
		if (l->IsRecording())
			l->Close(l->m_savedRenderStates);
		else
			NoteStopped(l);
	}
	m_uploadList = nullptr;
	if (m_pendingLists.IsEmpty())
		return;
	AutoArray<ID3D12CommandList*>	execList(m_pendingLists.Length());
	int								n = 0;
#if LOG_EXECUTION //def _DEBUG
	logHandler.Print("\nCommandListHandler::ExecuteAll: executing %u command lists.\n", (unsigned)m_pendingLists.Length());
#endif
	for (auto l : m_pendingLists) {
#if LOG_EXECUTION //def _DEBUG
		if (l->m_isRecording) {
			logHandler.Print("   '%s' still open; closing it now.\n", (const char*)l->GetName());
			l->Close();
		}
		logHandler.Print("   executing CommandList '%s' (CL:%p, list:%p).\n", (const char*)l->GetName(), (void*)l, (void*)l->GfxList());
#endif
		if (not l->IsFlushed())
			execList[n++] = l->GfxList(true);
	}
#if LOG_EXECUTION //def _DEBUG
	logHandler.Print("\n");
#endif
	if (n > 0)
		m_cmdQueue.Queue()->ExecuteCommandLists(UINT(n), execList.DataPtr());
	HandleDeviceLost("CommandListHandler::ExecuteAll");
#if DBG_DIRECTX
	gfxStates.CheckError();
	HRESULT removed = dx12Context.Device() ? dx12Context.Device()->GetDeviceRemovedReason() : E_FAIL;
	if (FAILED(removed)) {
		logHandler.Print("CommandListHandler::ExecuteAll: device removed (0x%08X)\n", (unsigned)removed);
		dx12Context.DumpDRED();
	}
#endif
	for (auto l : m_pendingLists) {
		if (l->m_isTemporary)
			m_recycledLists.Push(l);
	}
	m_pendingLists.Clear();
	m_cmdListStack.Clear();
}


void CommandListHandler::ExecutePending(void)
noexcept
{
	ZoneScoped;
	if (m_uploadList) {
		m_uploadList->Close();
		m_uploadList = nullptr;
	}
	if (m_pendingLists.IsEmpty())
		return;
	AutoArray<ID3D12CommandList*>	execList(m_pendingLists.Length());
	int								n = 0;
	for (auto l : m_pendingLists) {
		if (not l->IsFlushed())
			execList[n++] = l->GfxList(true);
	}
	if (n > 0)
		m_cmdQueue.Queue()->ExecuteCommandLists(UINT(n), execList.DataPtr());
#if DBG_DIRECTX
	gfxStates.CheckError();
#endif
	CloseProfilerQueries(true);
	m_cmdQueue.WaitIdle();
	TracyD3D12Collect(m_gpuProfilerCtx);
	for (auto l : m_pendingLists) {
		if (l->m_isTemporary)
			m_recycledLists.Push(l);
	}
	m_pendingLists.Clear();
	DrainFrameResources();
}


void CommandListHandler::DrainFrameResources(void)
noexcept
{
	CommandList*	oldestList = OldestRecordingList();
	uint64_t		oldestOpenSerial = oldestList ? oldestList->m_openSerial : UINT64_MAX;

	if (gfxResourceHandler.LastAllocSerial() < oldestOpenSerial) {
		cbvAllocator.Reset(UINT(m_frameIndex));
		descriptorHeaps.ResetTables(uint32_t(m_frameIndex));
	}
	ReleaseFrameResources();
	ResetBindings();
}


void CommandListHandler::NoteRecording(CommandList* cl)
noexcept
{
	for (auto l : m_recordingLists) {
		if (l == cl)
			return;
	}
	m_recordingLists.Push(cl);
}


void CommandListHandler::NoteStopped(CommandList* cl)
noexcept
{
	int32_t last = m_recordingLists.Length() - 1;

	for (int32_t i = 0; i <= last; ++i) {
		if (m_recordingLists[i] == cl) {
			m_recordingLists[i] = m_recordingLists[last];
			m_recordingLists.Pop();
			return;
		}
	}
}


CommandList* CommandListHandler::OldestRecordingList(void) const
noexcept
{
	CommandList* oldest = nullptr;

	for (auto l : m_recordingLists) {
		if (l->IsRecording() and (not oldest or (l->m_openSerial < oldest->m_openSerial)))
			oldest = l;
	}
	return oldest;
}


void CommandListHandler::ReleaseFrameResources(void)
noexcept
{
	CommandList* oldestList = OldestRecordingList();

	gfxResourceHandler.CleanupBefore(m_frameIndex, oldestList ? oldestList->m_openSerial : UINT64_MAX);
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


CommandList* CommandListHandler::UploadCmdList(void)
noexcept
{
	if (not m_uploadList) {
		CommandList* cl = CreateCmdList(String("Upload"), true);
		if (not cl)
			return nullptr;
		if (not cl->Open(false, true)) {
			m_recycledLists.Push(cl);
			return nullptr;
		}
		m_uploadList = cl;
	}
	return m_uploadList;
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
	ID3D12Device* device = dx12Context.Device();
	if (not device)
		return nullptr;

	CommandList* cl = new CommandList();
	if (not cl->Create(device, name, isTemporary)) {
		delete cl;
		return nullptr;
	}
	cl->Reset();
	++m_cmdListCount;
	return cl;
}


static constexpr uint32_t	kGpuFrameSlotSize = 256;
static constexpr uint32_t	kGpuFrameCycleMax = kGpuFrameSlotSize / 2;
static constexpr uint32_t	kGpuFrameStageMax = 128;
static constexpr uint32_t	kGpuFrameQueries = 2 + kGpuFrameStageMax;
static constexpr size_t		kGpuFrameNameSize = 48;
static constexpr uint32_t	kGpuFrameEntryMax = 128;
static constexpr uint16_t	kGpuFrameOtherLists = 0;
static constexpr uint16_t	kGpuFrameOtherStages = 1;
static constexpr double		kGpuFrameMinMs = 0.1;
static constexpr double		kGpuFrameSummaryMs = 1000.0;
static constexpr size_t		kGpuFrameTextSize = 8192;

struct DxGpuFrameRecord {
	const CommandList*			list;
	ID3D12GraphicsCommandList*	gfxList;
	bool						isEnded;
	uint16_t					entry;
	uint32_t					stageCount;
	uint32_t					stagesDropped;
	uint16_t					stageEntries[kGpuFrameStageMax];
};

struct DxGpuFrameSlot {
	uint32_t			first{ 0 };
	uint32_t			count{ 0 };
	uint32_t			dropped{ 0 };
	uint64_t			frameNumber{ 0 };
	uint32_t			previousFirst{ 0 };
	uint32_t			previousCount{ 0 };
	uint32_t			previousDropped{ 0 };
	uint64_t			previousFrameNumber{ 0 };
	DxGpuFrameRecord	records[kGpuFrameSlotSize]{};
};

struct DxGpuFrameEntry {
	char	name[kGpuFrameNameSize];
	bool	isStage;
	double	frameMs;
	double	totalMs;
	double	maxMs;
};

struct DxGpuFrameSummary {
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
	DxGpuFrameEntry	entries[kGpuFrameEntryMax]{};
};

static bool						dxGpuFrameOn = false;
static ComPtr<ID3D12QueryHeap>	dxGpuFrameHeap;
static ComPtr<ID3D12Resource>	dxGpuFrameReadback;
static bool						dxGpuFrameUnavailable = false;
static double					dxGpuFrameTickMs = 0.0;
static DxGpuFrameSlot			dxGpuFrameSlots[CommandQueue::FRAME_COUNT];
static DxGpuFrameSummary		dxGpuFrameSummary;
static char						dxGpuFrameText[kGpuFrameTextSize];


static double DxGpuFrameClock(void)
noexcept
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}


static inline uint32_t DxGpuFrameQuery(uint32_t slotIndex, uint32_t index)
noexcept
{
	return (slotIndex * kGpuFrameSlotSize + index) * kGpuFrameQueries;
}


static void DxGpuFrameSetEntry(DxGpuFrameEntry& entry, const char* name, bool isStage)
noexcept
{
	snprintf(entry.name, sizeof(entry.name), "%s", name);
	entry.isStage = isStage;
	entry.frameMs = 0.0;
	entry.totalMs = 0.0;
	entry.maxMs = 0.0;
}


static uint16_t DxGpuFrameEntryIndex(const char* name, bool isStage)
noexcept
{
	DxGpuFrameSummary&	summary = dxGpuFrameSummary;
	char				key[kGpuFrameNameSize];
	size_t				length = 0;

	if (summary.entryCount == 0) {
		DxGpuFrameSetEntry(summary.entries[kGpuFrameOtherLists], "(other lists)", false);
		DxGpuFrameSetEntry(summary.entries[kGpuFrameOtherStages], "(other stages)", true);
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
	DxGpuFrameSetEntry(summary.entries[summary.entryCount], key, isStage);
	return uint16_t(summary.entryCount++);
}


static DxGpuFrameRecord* DxGpuFrameFind(DxGpuFrameSlot& slot, const CommandList* list, uint32_t& index)
noexcept
{
	for (uint32_t i = slot.count; i > 0; --i) {
		index = (slot.first + i - 1) % kGpuFrameSlotSize;
		DxGpuFrameRecord& record = slot.records[index];
		if ((record.list == list) and not record.isEnded)
			return &record;
	}
	return nullptr;
}


static bool DxGpuFrameCreate(void)
noexcept
{
	if (dxGpuFrameHeap.Get() != nullptr)
		return true;
	if (dxGpuFrameUnavailable)
		return false;
	UINT64	frequency = 0;
	HRESULT	hr = commandListHandler.GetQueue()->GetTimestampFrequency(&frequency);
	if (FAILED(hr) or (frequency == 0)) {
		logHandler.Print("GPU frame timing: the graphics queue has no timestamps (hr=0x%08X)\n", unsigned(hr));
		dxGpuFrameUnavailable = true;
		return false;
	}
	ID3D12Device*			device = dx12Context.Device();
	D3D12_QUERY_HEAP_DESC	heapDesc{};
	heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	heapDesc.Count = CommandQueue::FRAME_COUNT * kGpuFrameSlotSize * kGpuFrameQueries;
	hr = device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&dxGpuFrameHeap));
	if (FAILED(hr)) {
		logHandler.Print("GPU frame timing: CreateQueryHeap failed (hr=0x%08X)\n", unsigned(hr));
		dxGpuFrameHeap.Reset();
		dxGpuFrameUnavailable = true;
		return false;
	}
	D3D12_HEAP_PROPERTIES	heapProps{};
	D3D12_RESOURCE_DESC		bufferDesc{};
	heapProps.Type = D3D12_HEAP_TYPE_READBACK;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = UINT64(heapDesc.Count) * sizeof(uint64_t);
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
										 IID_PPV_ARGS(&dxGpuFrameReadback));
	if (FAILED(hr)) {
		logHandler.Print("GPU frame timing: the readback buffer cannot be created (hr=0x%08X)\n", unsigned(hr));
		dxGpuFrameHeap.Reset();
		dxGpuFrameReadback.Reset();
		dxGpuFrameUnavailable = true;
		return false;
	}
	dxGpuFrameTickMs = 1000.0 / double(frequency);
	return true;
}


static void DxGpuFrameBegin(const CommandList* list, const char* name, ID3D12GraphicsCommandList* gfxList, uint32_t slotIndex)
noexcept
{
	if (not (dxGpuFrameOn and DxGpuFrameCreate()))
		return;
	DxGpuFrameSlot& slot = dxGpuFrameSlots[slotIndex];
	if (slot.count >= kGpuFrameCycleMax) {
		++slot.dropped;
		return;
	}
	uint32_t index = (slot.first + slot.count) % kGpuFrameSlotSize;
	++slot.count;
	DxGpuFrameRecord& record = slot.records[index];
	record.list = list;
	record.gfxList = gfxList;
	record.isEnded = false;
	record.entry = DxGpuFrameEntryIndex(name, false);
	record.stageCount = 0;
	record.stagesDropped = 0;
	gfxList->EndQuery(dxGpuFrameHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, DxGpuFrameQuery(slotIndex, index));
}


static void DxGpuFrameStage(ID3D12GraphicsCommandList* gfxList, const char* name)
noexcept
{
	if ((dxGpuFrameHeap.Get() == nullptr) or (gfxList == nullptr))
		return;
	for (uint32_t slotIndex = 0; slotIndex < CommandQueue::FRAME_COUNT; ++slotIndex) {
		DxGpuFrameSlot& slot = dxGpuFrameSlots[slotIndex];
		for (uint32_t i = slot.count; i > 0; --i) {
			uint32_t			index = (slot.first + i - 1) % kGpuFrameSlotSize;
			DxGpuFrameRecord&	record = slot.records[index];
			if ((record.gfxList != gfxList) or record.isEnded)
				continue;
			if (record.stageCount >= kGpuFrameStageMax) {
				++record.stagesDropped;
				return;
			}
			record.stageEntries[record.stageCount] = DxGpuFrameEntryIndex(name, true);
			gfxList->EndQuery(dxGpuFrameHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, DxGpuFrameQuery(slotIndex, index) + 2 + record.stageCount);
			++record.stageCount;
			return;
		}
	}
}


static void DxGpuFrameEnd(const CommandList* list, ID3D12GraphicsCommandList* gfxList, uint32_t slotIndex)
noexcept
{
	if (dxGpuFrameHeap.Get() == nullptr)
		return;
	uint32_t			index = 0;
	DxGpuFrameRecord*	record = DxGpuFrameFind(dxGpuFrameSlots[slotIndex], list, index);
	if (record == nullptr)
		return;
	record->isEnded = true;
	uint32_t query = DxGpuFrameQuery(slotIndex, index);
	gfxList->EndQuery(dxGpuFrameHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1);
	gfxList->ResolveQueryData(dxGpuFrameHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query, 2 + record->stageCount, dxGpuFrameReadback.Get(),
							  UINT64(query) * sizeof(uint64_t));
}


static void DxGpuFrameAdvance(uint32_t slotIndex)
noexcept
{
	DxGpuFrameSlot& slot = dxGpuFrameSlots[slotIndex];
	slot.previousFirst = slot.first;
	slot.previousCount = slot.count;
	slot.previousDropped = slot.dropped;
	slot.previousFrameNumber = slot.frameNumber;
	slot.first = (slot.first + slot.count) % kGpuFrameSlotSize;
	slot.count = 0;
	slot.dropped = 0;
}


static void DxGpuFrameAppend(int& length, const char* format, ...)
noexcept
{
	if (length >= int(kGpuFrameTextSize) - 1)
		return;
	va_list	args;
	va_start(args, format);
	int written = vsnprintf(dxGpuFrameText + length, kGpuFrameTextSize - size_t(length), format, args);
	va_end(args);
	if (written <= 0)
		return;
	length += written;
	if (length > int(kGpuFrameTextSize) - 1)
		length = int(kGpuFrameTextSize) - 1;
}


static void DxGpuFrameAppendEntries(int& length, bool isStage, bool isSummary)
noexcept
{
	const DxGpuFrameSummary&	summary = dxGpuFrameSummary;
	bool						isListed[kGpuFrameEntryMax]{};
	const char*					separator = isStage ? "; stages:" : "; lists:";

	for (;;) {
		int		best = -1;
		double	bestMs = kGpuFrameMinMs;
		for (uint32_t i = 0; i < summary.entryCount; ++i) {
			const DxGpuFrameEntry&	entry = summary.entries[i];
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
			DxGpuFrameAppend(length, "%s %s %.2f/%.2f", separator, summary.entries[best].name, bestMs, summary.entries[best].maxMs);
		else
			DxGpuFrameAppend(length, "%s %s %.2f", separator, summary.entries[best].name, bestMs);
		separator = ",";
	}
}


static void DxGpuFrameLogFrame(uint64_t frameNumber, double periodMs, double busyMs, double spanMs, uint32_t timed, uint32_t count,
							   uint32_t dropped, uint32_t stagesDropped)
noexcept
{
	int length = 0;
	DxGpuFrameAppend(length, "GPU frame %llu over the display period of %.2f ms: busy %.2f ms, span %.2f ms, %u of %u lists timed, %u lists and %u stages not recorded",
					 static_cast<unsigned long long>(frameNumber), periodMs, busyMs, spanMs, timed, count, dropped, stagesDropped);
	DxGpuFrameAppendEntries(length, false, false);
	DxGpuFrameAppendEntries(length, true, false);
	logHandler.Print("%s\n", dxGpuFrameText);
}


static void DxGpuFrameLogSummary(double elapsedMs)
noexcept
{
	const DxGpuFrameSummary&	summary = dxGpuFrameSummary;
	double						frames = double(summary.frames);
	int							length = 0;
	DxGpuFrameAppend(length, "GPU %u frames in %.2f s, display period %.2f ms: busy avg %.2f max %.2f ms, span avg %.2f max %.2f ms, %u frames busy longer than the period, %u lists and %u stages not recorded",
					 summary.frames, elapsedMs * 0.001, summary.periodMs, summary.busyTotalMs / frames, summary.busyMaxMs, summary.spanTotalMs / frames,
					 summary.spanMaxMs, summary.overPeriod, summary.listsDropped, summary.stagesDropped);
	DxGpuFrameAppendEntries(length, false, true);
	DxGpuFrameAppendEntries(length, true, true);
	logHandler.Print("%s\n", dxGpuFrameText);
}


static void DxGpuFrameResetSummary(double startMs)
noexcept
{
	DxGpuFrameSummary& summary = dxGpuFrameSummary;
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


static void DxGpuFrameMeasure(uint32_t slotIndex, uint32_t first, uint32_t count, uint32_t dropped, uint64_t frameNumber)
noexcept
{
	const DxGpuFrameSlot&	slot = dxGpuFrameSlots[slotIndex];
	DxGpuFrameSummary&		summary = dxGpuFrameSummary;
	uint64_t				firstBegin = UINT64_MAX;
	uint64_t				lastEnd = 0;
	double					busyMs = 0.0;
	uint32_t				timed = 0;
	uint32_t				stagesDropped = 0;
	void*					mapped = nullptr;
	D3D12_RANGE				readRange{ SIZE_T(DxGpuFrameQuery(slotIndex, 0)) * sizeof(uint64_t), SIZE_T(DxGpuFrameQuery(slotIndex + 1, 0)) * sizeof(uint64_t) };

	if (FAILED(dxGpuFrameReadback->Map(0, &readRange, &mapped)))
		return;

	const uint64_t* data = static_cast<const uint64_t*>(mapped);

	for (uint32_t i = 0; i < count; ++i) {
		uint32_t				index = (first + i) % kGpuFrameSlotSize;
		const DxGpuFrameRecord&	record = slot.records[index];
		if (not record.isEnded)
			continue;
		uint32_t	query = DxGpuFrameQuery(slotIndex, index);
		uint64_t	begin = data[query];
		uint64_t	end = data[query + 1];
		if (end < begin)
			continue;
		double listMs = double(end - begin) * dxGpuFrameTickMs;
		summary.entries[record.entry].frameMs += listMs;
		busyMs += listMs;
		++timed;
		if (firstBegin > begin)
			firstBegin = begin;
		if (lastEnd < end)
			lastEnd = end;
		stagesDropped += record.stagesDropped;
		for (uint32_t j = 0; j < record.stageCount; ++j) {
			uint64_t stage = data[query + 2 + j];
			uint64_t stageEnd = (j + 1 < record.stageCount) ? data[query + 3 + j] : end;
			if (stageEnd >= stage)
				summary.entries[record.stageEntries[j]].frameMs += double(stageEnd - stage) * dxGpuFrameTickMs;
		}
	}

	D3D12_RANGE writeRange{ 0, 0 };

	dxGpuFrameReadback->Unmap(0, &writeRange);
	if (timed == 0)
		return;
	double	spanMs = double(lastEnd - firstBegin) * dxGpuFrameTickMs;
	double	periodMs = xrHandler.DisplayPeriod();
	if ((periodMs > 0.0) and (busyMs > periodMs)) {
		++summary.overPeriod;
		DxGpuFrameLogFrame(frameNumber, periodMs, busyMs, spanMs, timed, count, dropped, stagesDropped);
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
		DxGpuFrameEntry& entry = summary.entries[i];
		entry.totalMs += entry.frameMs;
		if (entry.maxMs < entry.frameMs)
			entry.maxMs = entry.frameMs;
		entry.frameMs = 0.0;
	}
	double now = DxGpuFrameClock();
	if (summary.startMs == 0.0)
		summary.startMs = now;
	else if (now - summary.startMs >= kGpuFrameSummaryMs) {
		DxGpuFrameLogSummary(now - summary.startMs);
		DxGpuFrameResetSummary(now);
	}
}


static void DxGpuFrameSwitch(bool isOn)
noexcept
{
	if (isOn == dxGpuFrameOn)
		return;
	dxGpuFrameOn = isOn;
	if (isOn)
		DxGpuFrameResetSummary(0.0);
	logHandler.Print("GPU frame timing %s\n", isOn ? "on" : "off");
}


static void DxGpuFrameResolve(uint32_t slotIndex, uint64_t frameNumber)
noexcept
{
	DxGpuFrameSlot&	slot = dxGpuFrameSlots[slotIndex];
	uint32_t		first = slot.previousFirst;
	uint32_t		count = slot.previousCount;
	uint32_t		dropped = slot.previousDropped;
	uint64_t		recordFrame = slot.previousFrameNumber;
	slot.previousCount = 0;
	slot.previousDropped = 0;
	slot.frameNumber = frameNumber;
	if ((dxGpuFrameHeap.Get() != nullptr) and (count > 0))
		DxGpuFrameMeasure(slotIndex, first, count, dropped, recordFrame);
	DxGpuFrameSwitch(xrHandler.IsRunning());
}


static void DxGpuFrameDestroy(void)
noexcept
{
	dxGpuFrameHeap.Reset();
	dxGpuFrameReadback.Reset();
	for (DxGpuFrameSlot& slot : dxGpuFrameSlots) {
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
	DxGpuFrameStage(commandListHandler.CurrentGfxList(), name);
}


void GfxDumpConstants(const char* label)
noexcept
{
}


bool GfxSampleFrame(uint64_t& frameNumber)
noexcept
{
	frameNumber = commandListHandler.FrameNumber();
	return false;
}

// =================================================================================================
