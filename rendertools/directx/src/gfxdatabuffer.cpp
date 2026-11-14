#define NOMINMAX

#include "gfxdatabuffer.h"
#include "commandlist.h"
#include "dx12context.h"
#include "dx12upload.h"
#include "resource_handler.h"
#include "shader_loading.h"
#include "loghandler.h"
#include <cstdio>
#include <cstring>

// =================================================================================================
// DX12 GfxDataBuffer implementation
//
// Dynamic data is kept on an upload-heap committed resource (CPU-writable, GPU-readable).
// Static data is copied from an upload buffer into a default-heap resource, so the GPU reads it
// from video memory instead of across the bus.

static_assert(GfxDataBuffer::FRAME_COUNT == CommandList::FRAME_COUNT,
			  "GfxDataBuffer upload-slot count must match the command-list frame count");


static DXGI_FORMAT IndexFormatFromComponentType(ComponentType componentType)
noexcept
{
	return (componentType == ComponentType::UInt16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}


GfxDataBuffer::GfxDataBuffer(const char* type, int id, GfxBufferTarget bufferType, bool isDynamic)
noexcept
	: m_index(-1)
	, m_id(id)
	, m_type(type)
	, m_bufferType(bufferType)
	, m_data(nullptr)
	, m_size(0)
	, m_itemSize(0)
	, m_itemCount(0)
	, m_componentCount(0)
	, m_componentType(ComponentType::Float)
	, m_isDynamic(isDynamic)
{}


size_t GfxDataBuffer::ComponentSize(size_t componentType)
noexcept
{
	switch (ComponentType(componentType)) {
		case ComponentType::UInt16:
			return 2;
		case ComponentType::Float:
		case ComponentType::UInt32:
		default:
			return 4;
	}
}


GfxDataBuffer& GfxDataBuffer::Copy(GfxDataBuffer const& other)
{
	if (this != &other) {
		ResourceDescriptor::operator=(other);
		m_index = other.m_index;
		m_type = other.m_type;
		m_id = other.m_id;
		m_bufferType = other.m_bufferType;
		m_data = other.m_data;
		for (int i = 0; i < FRAME_COUNT; ++i)
			m_resource[i] = other.m_resource[i]; // shared ref-count via ComPtr
		m_vbv = other.m_vbv;
		m_ibv = other.m_ibv;
		m_size = other.m_size;
		m_itemSize = other.m_itemSize;
		m_itemCount = other.m_itemCount;
		m_componentCount = other.m_componentCount;
		m_componentType = other.m_componentType;
		m_isDynamic = other.m_isDynamic;
		m_inDefaultHeap = other.m_inDefaultHeap;
		m_liveSlot = other.m_liveSlot;
		m_activeSlot = other.m_activeSlot;
		for (int i = 0; i < FRAME_COUNT; ++i) {
			m_slotRetiredFrame[i] = other.m_slotRetiredFrame[i];
			m_slotBoundFrame[i] = other.m_slotBoundFrame[i];
			m_slotWasBound[i] = other.m_slotWasBound[i];
		}
	}
	return *this;
}


GfxDataBuffer& GfxDataBuffer::Move(GfxDataBuffer& other)
noexcept
{
	if (this != &other) {
		ResourceDescriptor::operator=(std::move(other));
		m_index = other.m_index;
		m_type = other.m_type;
		m_id = other.m_id;
		m_bufferType = other.m_bufferType;
		m_data = other.m_data;
		for (int i = 0; i < FRAME_COUNT; ++i)
			m_resource[i] = std::move(other.m_resource[i]);
		m_vbv = other.m_vbv;
		m_ibv = other.m_ibv;
		other.m_vbv = {};
		other.m_ibv = {};
		m_size = other.m_size;
		m_itemSize = other.m_itemSize;
		m_itemCount = other.m_itemCount;
		m_componentCount = other.m_componentCount;
		m_componentType = other.m_componentType;
		m_isDynamic = other.m_isDynamic;
		m_inDefaultHeap = other.m_inDefaultHeap;
		m_liveSlot = other.m_liveSlot;
		m_activeSlot = other.m_activeSlot;
		for (int i = 0; i < FRAME_COUNT; ++i) {
			m_slotRetiredFrame[i] = other.m_slotRetiredFrame[i];
			m_slotBoundFrame[i] = other.m_slotBoundFrame[i];
			m_slotWasBound[i] = other.m_slotWasBound[i];
		}
	}
	return *this;
}


bool GfxDataBuffer::Create(int slot, size_t dataSize)
{
	m_resource[slot] = gfxResourceHandler.AcquireUpload(dataSize);
	if (not m_resource[slot]) {
		logHandler.Print("GfxDataBuffer::Create: no upload buffer (size=%zu, type=%s/%d slot %d)\n", dataSize, m_type ? m_type : "?",
						 m_id, slot);
		return false;
	}
#if DBG_DIRECTX
	char name[128];
	snprintf(name, sizeof(name), "GfxDataBuffer[%s/%d] slot %d", m_type, m_id, slot);
	m_resource[slot]->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(strlen(name)), name);
#endif
	return true;
}


bool GfxDataBuffer::CreateStatic(size_t dataSize)
{
	D3D12_HEAP_PROPERTIES	hp{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_RESOURCE_DESC		rd{};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = dataSize;
	rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
	rd.SampleDesc.Count = 1;
	rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = dx12Context.Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
															   IID_PPV_ARGS(&m_resource[0]));
	if (FAILED(hr)) {
		logHandler.Print("GfxDataBuffer::CreateStatic: CreateCommittedResource failed (size=%zu, type=%s/%d, hr=0x%08X)\n", dataSize,
						 m_type ? m_type : "?", m_id, unsigned(hr));
		return false;
	}
#if DBG_DIRECTX
	char name[128];
	snprintf(name, sizeof(name), "GfxDataBuffer[%s/%d] static", m_type, m_id);
	m_resource[0]->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(strlen(name)), name);
#endif
	m_inDefaultHeap = true;
	return true;
}


void GfxDataBuffer::ReleaseSlot(int slot)
noexcept
{
	if (m_resource[slot]) {
		if ((slot == 0) and m_inDefaultHeap)
			gfxResourceHandler.Track(m_resource[slot]);
		else
			gfxResourceHandler.TrackUpload(m_resource[slot]);
		m_resource[slot].Reset();
	}
	if (slot == 0)
		m_inDefaultHeap = false;
}


bool GfxDataBuffer::Update(const char* type, GfxBufferTarget bufferType, int index, void* data, size_t dataSize,
						   ComponentType componentType, size_t componentCount, bool /*forceUpdate*/)
noexcept
{
	if (not data or (dataSize == 0))
		return false;

	ID3D12Device* device = dx12Context.Device();
	if (not device)
		return false;

	m_type = type;
	m_bufferType = bufferType;
	m_index = index;
	m_componentType = componentType;
	m_componentCount = int(componentCount);

	m_itemSize = ComponentSize(size_t(componentType)) * componentCount;
	m_itemCount = uint32_t(dataSize / ((m_itemSize > 0) ? m_itemSize : 1));
	m_size = uint32_t(dataSize);

	// Dynamic buffers rotate through FRAME_COUNT upload slots so a write never lands on a
	// resource the GPU may still be reading from a previous in-flight frame; static buffers
	// always use slot 0. Each slot is created once and reused — no per-frame allocation.
	//
	// A second Update of the same buffer within one frame (e.g. MapLayout drawing every map's
	// layout during init, with no frame boundary between them) would otherwise overwrite a
	// slot still referenced by an already-recorded draw — so a same-frame re-update takes a
	// fresh resource. The previous one stays alive via gfxResourceHandler's per-frame tracking
	// until the next Cleanup / FlushResources.
#if !OPTIMIZE_BUFFER_REUSE
	//
	// A static buffer has no slot rotation to protect it, so ANY update of one that already holds
	// data is treated the same way: the frame before may still be in flight with a draw that reads
	// slot 0, and writing the new data into it would hand that draw the next frame's vertices.
	//
	// Rotation alone is NOT enough, and this is what the slot bookkeeping below is for: a buffer is
	// read by every frame from the update that wrote it until the next one, not just by the frame it
	// was written in. A buffer that is updated only now and then - the level mesh's index buffer is
	// rewritten only when the batch layout changes - is therefore still being drawn from in the frame
	// before this one, out of the very slot this frame's index picks next. BeginFrame waits for the
	// frame with THIS index, never for the one in between, so writing there hands a draw that is
	// still in flight half of the next layout. A slot may be written in place only when it is not
	// the live one and the frames in flight since it was retired are through.
#endif
	const uint64_t	frameNumber = commandListHandler.FrameNumber();
	const uint64_t	framesInFlight = uint64_t(commandListHandler.FrameCount());
	const int		slot = m_isDynamic ? commandListHandler.FrameIndex() : 0;
#if OPTIMIZE_BUFFER_REUSE
	const bool slotIsBusy = m_resource[slot] and m_slotWasBound[slot] and (m_slotBoundFrame[slot] + framesInFlight > frameNumber);
	const bool toDefaultHeap = not m_isDynamic and not slotIsBusy;
	const bool heapMatches = (slot != 0) or (m_inDefaultHeap == toDefaultHeap);
	if (slotIsBusy or not m_resource[slot] or (m_resource[slot]->GetDesc().Width < dataSize) or not heapMatches) {
		ReleaseSlot(slot);
		if (not (toDefaultHeap ? CreateStatic(dataSize) : Create(slot, dataSize)))
			return false;
		m_slotWasBound[slot] = false;
	}
#else
	const bool slotIsBusy = m_resource[slot] and ((slot == m_liveSlot) or (m_slotRetiredFrame[slot] + framesInFlight > frameNumber));
	const bool needsFreshBuffer = slotIsBusy or not m_isDynamic;
	const bool toDefaultHeap = not m_isDynamic and not slotIsBusy;
	const bool heapMatches = (slot != 0) or (m_inDefaultHeap == toDefaultHeap);
	if (needsFreshBuffer or not m_resource[slot] or (m_resource[slot]->GetDesc().Width < dataSize) or not heapMatches) {
		ReleaseSlot(slot);
		if (not (toDefaultHeap ? CreateStatic(dataSize) : Create(slot, dataSize)))
			return false;
	}
	if ((m_liveSlot >= 0) and (m_liveSlot != slot))
		m_slotRetiredFrame[m_liveSlot] = frameNumber;
	m_liveSlot = slot;
#endif
	m_activeSlot = slot;
	ID3D12Resource* resource = m_resource[slot].Get();

	// Upload data
	if (toDefaultHeap) {
		if (not UploadBufferData(resource, data, dataSize))
			return false;
	}
	else {
		void*		mapped = nullptr;
		D3D12_RANGE	range{ 0, 0 };
		HRESULT hr = resource->Map(0, &range, &mapped);
		if (FAILED(hr)) {
			logHandler.Print("GfxDataBuffer::Update: Map failed (size=%zu, type=%s/%d slot %d, hr=0x%08X)\n", dataSize,
							 m_type ? m_type : "?", m_id, slot, unsigned(hr));
			return false;
		}
		std::memcpy(mapped, data, dataSize);
		resource->Unmap(0, nullptr);
	}

	D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = resource->GetGPUVirtualAddress();

	if (bufferType == GfxBufferTarget::Index) {
		m_ibv.BufferLocation = gpuAddr;
		m_ibv.SizeInBytes = UINT(dataSize);
		m_ibv.Format = IndexFormatFromComponentType(componentType);
	}
	else {
		m_vbv.BufferLocation = gpuAddr;
		m_vbv.SizeInBytes = UINT(dataSize);
		m_vbv.StrideInBytes = UINT(m_itemSize);
	}

	return true;
}


void GfxDataBuffer::Clear(void)
noexcept
{
	for (int i = 0; i < FRAME_COUNT; ++i)
		ReleaseSlot(i);
	m_vbv = {};
	m_ibv = {};
	m_id = 0;
	m_isDynamic = true;
}


void GfxDataBuffer::Destroy(void)
noexcept
{
	for (int i = 0; i < FRAME_COUNT; ++i)
		ReleaseSlot(i);
	m_vbv = {};
	m_ibv = {};
	m_size = 0;
	m_itemCount = 0;
	m_itemSize = 0;
}

// =================================================================================================
