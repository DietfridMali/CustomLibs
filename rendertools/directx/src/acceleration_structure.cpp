#include "dx12framework.h"

#include "acceleration_structure.h"
#include "dx12context.h"
#include "commandlist.h"
#include "resource_handler.h"
#include "loghandler.h"

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

// =================================================================================================

namespace {
uint32_t	g_scratchAlignment{ D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT };
bool		g_loaded{ false };
bool		g_enhancedBarriers{ false };

ComPtr<ID3D12Device5> RayTracingDevice(void)
noexcept
{
	ComPtr<ID3D12Device5>	device;
	ID3D12Device*			baseDevice = dx12Context.Device();
	if (baseDevice)
		baseDevice->QueryInterface(IID_PPV_ARGS(device.GetAddressOf()));
	return device;
}

ComPtr<ID3D12GraphicsCommandList4> RayTracingList(ID3D12GraphicsCommandList* list)
noexcept
{
	ComPtr<ID3D12GraphicsCommandList4> rayTracingList;
	if (list)
		list->QueryInterface(IID_PPV_ARGS(rayTracingList.GetAddressOf()));
	return rayTracingList;
}

bool CreateDefaultBuffer(ComPtr<ID3D12Resource>& buffer, UINT64 size, D3D12_RESOURCE_STATES state,
						 [[maybe_unused]] const char* name)
noexcept
{
	ID3D12Device* device = dx12Context.Device();
	if (not device or (size == 0))
		return false;

	D3D12_HEAP_PROPERTIES	hp{ D3D12_HEAP_TYPE_DEFAULT };
	D3D12_RESOURCE_DESC		rd{};
	rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = size;
	rd.Height = 1;
	rd.DepthOrArraySize = 1;
	rd.MipLevels = 1;
	rd.Format = DXGI_FORMAT_UNKNOWN;
	rd.SampleDesc.Count = 1;
	rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
											   IID_PPV_ARGS(buffer.ReleaseAndGetAddressOf()))))
		return false;
#if DBG_DIRECTX
	buffer->SetPrivateData(WKPDID_D3DDebugObjectName, UINT(std::strlen(name)), name);
#endif
	return true;
}

void ReleaseBuffer(ComPtr<ID3D12Resource>& buffer)
noexcept
{
	if (buffer and not GfxResourceHandler::IsShuttingDown())
		gfxResourceHandler.Track(buffer);
	buffer.Reset();
}

#if DBG_DIRECTX
void MarkInstanceBufferReleased(const ID3D12Resource* buffer)
noexcept;
#endif

void ReleaseUpload(ComPtr<ID3D12Resource>& buffer)
noexcept
{
#if DBG_DIRECTX
	MarkInstanceBufferReleased(buffer.Get());
#endif
	if (buffer and not GfxResourceHandler::IsShuttingDown())
		gfxResourceHandler.TrackUpload(buffer);
	buffer.Reset();
}

#if DBG_DIRECTX
constexpr uint32_t	kTopLevelRecordDescs = 32;
constexpr uint64_t	kTopLevelBuildRecords = 8;
constexpr int		kInstanceCheckLines = 32;

struct TopLevelBuildRecord {
	uint64_t						frameNumber{ 0 };
	int								frameIndex{ 0 };
	const AccelerationStructure*	structure{ nullptr };
	uint32_t						slot{ 0 };
	bool							immediate{ false };
	char							listName[64]{};
	uint32_t						instanceCount{ 0 };
	uint32_t						written{ 0 };
	uint32_t						capacity{ 0 };
	UINT64							resultSize{ 0 };
	UINT64							storageSize{ 0 };
	UINT64							scratchNeeded{ 0 };
	UINT64							scratchSize{ 0 };
	D3D12_GPU_VIRTUAL_ADDRESS		storage{ 0 };
	D3D12_GPU_VIRTUAL_ADDRESS		scratch{ 0 };
	D3D12_GPU_VIRTUAL_ADDRESS		instances{ 0 };
	ID3D12Resource*					instanceBuffer{ nullptr };
	bool							released{ false };
	uint32_t						descCount{ 0 };
	D3D12_RAYTRACING_INSTANCE_DESC	descs[kTopLevelRecordDescs]{};
	const CommandList*				list{ nullptr };
	uint64_t						listExecution{ 0 };
	uint32_t						markerSlot{ 0 };
	uint32_t						buildMarker{ 0 };
	uint32_t						drawsRecorded{ 0 };
};

constexpr uint32_t	kMarkerFields = 4;
constexpr UINT64	kMarkerBytes = kTopLevelBuildRecords * kMarkerFields * sizeof(uint32_t);

TopLevelBuildRecord		g_topLevelBuilds[kTopLevelBuildRecords];
uint64_t				g_topLevelBuildCount{ 0 };
int						g_instanceCheckLines{ 0 };
ComPtr<ID3D12Resource>	g_markerBuffer;
volatile uint32_t*		g_markers{ nullptr };
int						g_markerState{ 0 };

bool CreateMarkerBuffer(void)
noexcept
{
	if (g_markerState != 0)
		return g_markerState > 0;
	g_markerState = -1;

	ID3D12Device* device = dx12Context.Device();

	if (not device)
		return false;

	D3D12_HEAP_PROPERTIES	hp{};
	D3D12_RESOURCE_DESC		rd{};

	hp.Type = D3D12_HEAP_TYPE_CUSTOM;
	hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	hp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
	rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = kMarkerBytes;
	rd.Height = 1;
	rd.DepthOrArraySize = 1;
	rd.MipLevels = 1;
	rd.Format = DXGI_FORMAT_UNKNOWN;
	rd.SampleDesc.Count = 1;
	rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
												 IID_PPV_ARGS(g_markerBuffer.ReleaseAndGetAddressOf()));

	if (FAILED(hr)) {
		logHandler.Print("AccelerationStructure: marker buffer could not be created (0x%08X)\n", unsigned(hr));
		return false;
	}

	void* data = nullptr;

	hr = g_markerBuffer->Map(0, nullptr, &data);
	if (FAILED(hr) or not data) {
		logHandler.Print("AccelerationStructure: marker buffer could not be mapped (0x%08X)\n", unsigned(hr));
		g_markerBuffer.Reset();
		return false;
	}
	std::memset(data, 0, size_t(kMarkerBytes));
	g_markers = static_cast<uint32_t*>(data);
	g_markerState = 1;
	return true;
}


void WriteMarkers(ID3D12GraphicsCommandList* list, uint32_t markerSlot, const uint32_t* fields, const uint32_t* values,
				  const D3D12_WRITEBUFFERIMMEDIATE_MODE* modes, UINT count)
noexcept
{
	ComPtr<ID3D12GraphicsCommandList2> list2;

	if (not list or FAILED(list->QueryInterface(IID_PPV_ARGS(list2.GetAddressOf()))))
		return;

	D3D12_WRITEBUFFERIMMEDIATE_PARAMETER	params[kMarkerFields];
	D3D12_GPU_VIRTUAL_ADDRESS				base = g_markerBuffer->GetGPUVirtualAddress() + D3D12_GPU_VIRTUAL_ADDRESS(markerSlot) * kMarkerFields * sizeof(uint32_t);

	for (UINT i = 0; i < count; ++i) {
		params[i].Dest = base + D3D12_GPU_VIRTUAL_ADDRESS(fields[i]) * sizeof(uint32_t);
		params[i].Value = values[i];
	}
	list2->WriteBufferImmediate(count, params, modes);
}

void MarkInstanceBufferReleased(const ID3D12Resource* buffer)
noexcept
{
	if (not buffer)
		return;
	for (TopLevelBuildRecord& record : g_topLevelBuilds) {
		if (record.instanceBuffer == buffer)
			record.released = true;
	}
}


bool ReadInstanceDescs(ID3D12Resource* buffer, uint32_t count, D3D12_RAYTRACING_INSTANCE_DESC* descs, HRESULT& hr)
noexcept
{
	D3D12_RANGE	readRange{ 0, SIZE_T(count) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC) };
	D3D12_RANGE	writtenRange{ 0, 0 };
	void*		data = nullptr;

	hr = buffer->Map(0, &readRange, &data);
	if (FAILED(hr) or not data)
		return false;
	std::memcpy(descs, data, readRange.End);
	buffer->Unmap(0, &writtenRange);
	return true;
}


uint32_t FirstChangedDesc(const TopLevelBuildRecord& record, const D3D12_RAYTRACING_INSTANCE_DESC* current)
noexcept
{
	for (uint32_t i = 0; i < record.descCount; ++i) {
		if (std::memcmp(&record.descs[i], &current[i], sizeof(D3D12_RAYTRACING_INSTANCE_DESC)) != 0)
			return i;
	}
	return record.descCount;
}


void PrintChangedDesc(const char* where, uint64_t build, const TopLevelBuildRecord& record, uint32_t index,
					  const D3D12_RAYTRACING_INSTANCE_DESC& now)
noexcept
{
	const D3D12_RAYTRACING_INSTANCE_DESC& then = record.descs[index];

	logHandler.Print("%s instance buffer of build %llu (frame %llu, slot %u, list %s) changed, desc %u of %u: "
					 "recorded blas 0x%llX id %u mask 0x%02X flags %u at %g %g %g, "
					 "now blas 0x%llX id %u mask 0x%02X flags %u at %g %g %g\n",
					 where, static_cast<unsigned long long>(build), static_cast<unsigned long long>(record.frameNumber), record.slot,
					 record.listName, index, record.descCount, static_cast<unsigned long long>(then.AccelerationStructure),
					 unsigned(then.InstanceID), unsigned(then.InstanceMask), unsigned(then.Flags), then.Transform[0][3],
					 then.Transform[1][3], then.Transform[2][3], static_cast<unsigned long long>(now.AccelerationStructure),
					 unsigned(now.InstanceID), unsigned(now.InstanceMask), unsigned(now.Flags), now.Transform[0][3], now.Transform[1][3],
					 now.Transform[2][3]);
}


void CheckInstanceBuffer(const AccelerationStructure& as, ID3D12Resource* buffer)
noexcept
{
	uint64_t count = (g_topLevelBuildCount < kTopLevelBuildRecords) ? g_topLevelBuildCount : kTopLevelBuildRecords;

	for (uint64_t i = g_topLevelBuildCount; i > g_topLevelBuildCount - count; --i) {
		const TopLevelBuildRecord& record = g_topLevelBuilds[(i - 1) % kTopLevelBuildRecords];

		if ((record.structure != &as) or (record.slot != as.m_slot) or (record.instanceBuffer != buffer) or record.released)
			continue;

		D3D12_RAYTRACING_INSTANCE_DESC	current[kTopLevelRecordDescs];
		HRESULT							hr = S_OK;

		if (not ReadInstanceDescs(buffer, record.descCount, current, hr)) {
			if (g_instanceCheckLines < kInstanceCheckLines) {
				++g_instanceCheckLines;
				logHandler.Print("AccelerationStructure::BuildTopLevel: instance buffer of build %llu could not be mapped (0x%08X)\n",
								 static_cast<unsigned long long>(i - 1), unsigned(hr));
			}
			return;
		}

		uint32_t index = FirstChangedDesc(record, current);

		if ((index < record.descCount) and (g_instanceCheckLines < kInstanceCheckLines)) {
			++g_instanceCheckLines;
			PrintChangedDesc("AccelerationStructure::BuildTopLevel:", i - 1, record, index, current[index]);
		}
		return;
	}
}


void RecordTopLevelBuild(const AccelerationStructure& as, const CommandList& cl, bool immediate, uint32_t instanceCount,
						 const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& buildDesc,
						 const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& sizes)
noexcept
{
	TopLevelBuildRecord& record = g_topLevelBuilds[g_topLevelBuildCount % kTopLevelBuildRecords];

	++g_topLevelBuildCount;
	record.frameNumber = commandListHandler.FrameNumber();
	record.frameIndex = commandListHandler.FrameIndex();
	record.structure = &as;
	record.slot = as.m_slot;
	record.immediate = immediate;
	std::snprintf(record.listName, sizeof(record.listName), "%s", static_cast<const char*>(cl.GetName()));
	record.instanceCount = instanceCount;
	record.written = buildDesc.Inputs.NumDescs;
	record.capacity = as.m_instanceCapacity[as.m_slot];
	record.resultSize = sizes.ResultDataMaxSizeInBytes;
	record.storageSize = as.m_storageSize[as.m_slot];
	record.scratchNeeded = sizes.ScratchDataSizeInBytes;
	record.scratchSize = as.m_scratchSize[as.m_slot];
	record.storage = buildDesc.DestAccelerationStructureData;
	record.scratch = buildDesc.ScratchAccelerationStructureData;
	record.instances = buildDesc.Inputs.InstanceDescs;
	record.instanceBuffer = as.m_instances[as.m_slot].Get();
	record.released = false;
	record.descCount = (record.written < kTopLevelRecordDescs) ? record.written : kTopLevelRecordDescs;
	logHandler.Note("tlas build %llu: frame %llu, structure %p, slot %u, list %s, %u instances, storage 0x%llX",
					static_cast<unsigned long long>(g_topLevelBuildCount - 1), static_cast<unsigned long long>(record.frameNumber),
					static_cast<const void*>(record.structure), record.slot, record.listName, record.written,
					static_cast<unsigned long long>(record.storage));

	HRESULT hr = S_OK;

	if (not ReadInstanceDescs(record.instanceBuffer, record.descCount, record.descs, hr)) {
		if (g_instanceCheckLines < kInstanceCheckLines) {
			++g_instanceCheckLines;
			logHandler.Print("AccelerationStructure::BuildTopLevel: instance buffer of build %llu could not be read back (0x%08X)\n",
							 static_cast<unsigned long long>(g_topLevelBuildCount - 1), unsigned(hr));
		}
		record.descCount = 0;
	}

	record.list = &cl;
	record.listExecution = cl.GetExecutionCounter();
	record.markerSlot = uint32_t((g_topLevelBuildCount - 1) % kTopLevelBuildRecords);
	record.buildMarker = uint32_t(g_topLevelBuildCount);
	record.drawsRecorded = 0;
	if (not CreateMarkerBuffer())
		return;
	for (uint32_t i = 0; i < kMarkerFields; ++i)
		g_markers[record.markerSlot * kMarkerFields + i] = 0;

	const uint32_t							field = 0;
	const D3D12_WRITEBUFFERIMMEDIATE_MODE	mode = D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;

	WriteMarkers(cl.GfxList(), record.markerSlot, &field, &record.buildMarker, &mode, 1);
}
#endif
}

// =================================================================================================

bool RayTracingApi::Load(ID3D12Device* device)
noexcept
{
	if (g_loaded)
		return true;
	if (not device)
		return false;

	ComPtr<ID3D12Device5> rayTracingDevice;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(rayTracingDevice.GetAddressOf())))) {
		logHandler.Print("RayTracingApi::Load: ID3D12Device5 is not available\n");
		return false;
	}

	g_scratchAlignment = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;

	D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};

	g_enhancedBarriers = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12))) and
		(options12.EnhancedBarriersSupported != FALSE);
	logHandler.Print("RayTracingApi: enhanced barriers %s\n", g_enhancedBarriers ? "supported" : "not supported");
	g_loaded = true;
	return true;
}


bool RayTracingApi::IsLoaded(void)
noexcept
{
	return g_loaded;
}


uint32_t RayTracingApi::ScratchAlignment(void)
noexcept
{
	return g_scratchAlignment;
}

// =================================================================================================

namespace {
struct PreparedBuild {
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>			geoms;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC	buildDesc{};
	UINT64												scratchSize{ 0 };
	UINT64												scratchOffset{ 0 };
	AccelerationStructure*								target{ nullptr };
};


bool PrepareBottomLevel(AccelerationStructure& as, const AccelGeometryDesc* geometries,
						uint32_t geometryCount, PreparedBuild& out)
noexcept
{
	if ((not geometries) or (geometryCount == 0))
		return false;

	as.Destroy();

	uint32_t totalVertices = 0;
	uint32_t totalIndices = 0;
	for (uint32_t i = 0; i < geometryCount; ++i) {
		if ((not geometries[i].vertices) or (not geometries[i].indices))
			return false;
		if ((geometries[i].vertexCount == 0) or (geometries[i].indexCount < 3))
			return false;
		totalVertices += geometries[i].vertexCount;
		totalIndices += geometries[i].indexCount;
	}

	as.m_vertices = gfxResourceHandler.AcquireUpload(size_t(totalVertices) * 3 * sizeof(float));
	if (not as.m_vertices) {
		logHandler.Print("AccelerationStructure: vertex buffer allocation failed\n");
		as.Destroy();
		return false;
	}
	as.m_indices = gfxResourceHandler.AcquireUpload(size_t(totalIndices) * sizeof(uint32_t));
	if (not as.m_indices) {
		logHandler.Print("AccelerationStructure: index buffer allocation failed\n");
		as.Destroy();
		return false;
	}

	D3D12_RANGE	readRange{ 0, 0 };
	void*		vertexData = nullptr;
	void*		indexData = nullptr;
	if (FAILED(as.m_vertices->Map(0, &readRange, &vertexData)) or not vertexData) {
		logHandler.Print("AccelerationStructure: build input buffers could not be mapped\n");
		as.Destroy();
		return false;
	}
	if (FAILED(as.m_indices->Map(0, &readRange, &indexData)) or not indexData) {
		logHandler.Print("AccelerationStructure: build input buffers could not be mapped\n");
		as.m_vertices->Unmap(0, nullptr);
		as.Destroy();
		return false;
	}
	float*		pVertex = static_cast<float*>(vertexData);
	uint32_t*	pIndex = static_cast<uint32_t*>(indexData);

	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& geoms = out.geoms;

	geoms.resize(geometryCount);

	const D3D12_GPU_VIRTUAL_ADDRESS vertexBase = as.m_vertices->GetGPUVirtualAddress();
	const D3D12_GPU_VIRTUAL_ADDRESS indexBase = as.m_indices->GetGPUVirtualAddress();

	uint32_t vertexOffset = 0;
	uint32_t indexOffset = 0;
	for (uint32_t i = 0; i < geometryCount; ++i) {
		const AccelGeometryDesc&	src = geometries[i];
		const uint32_t				primitiveCount = src.indexCount / 3;

		std::memcpy(pVertex + size_t(vertexOffset) * 3, src.vertices, size_t(src.vertexCount) * 3 * sizeof(float));
		for (uint32_t j = 0; j < src.indexCount; ++j)
			pIndex[indexOffset + j] = src.indices[j] + vertexOffset;

		D3D12_RAYTRACING_GEOMETRY_DESC& geom = geoms[i];
		geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geom.Flags = src.opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
		geom.Triangles.Transform3x4 = 0;
		geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geom.Triangles.IndexCount = primitiveCount * 3;
		geom.Triangles.VertexCount = totalVertices;
		geom.Triangles.IndexBuffer = indexBase + D3D12_GPU_VIRTUAL_ADDRESS(indexOffset) * sizeof(uint32_t);
		geom.Triangles.VertexBuffer.StartAddress = vertexBase;
		geom.Triangles.VertexBuffer.StrideInBytes = 3 * sizeof(float);

		as.m_primitives += primitiveCount;
		vertexOffset += src.vertexCount;
		indexOffset += src.indexCount;
	}

	as.m_vertices->Unmap(0, nullptr);
	as.m_indices->Unmap(0, nullptr);

	out.target = &as;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = out.buildDesc.Inputs;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.NumDescs = geometryCount;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.pGeometryDescs = geoms.data();

	ComPtr<ID3D12Device5> device = RayTracingDevice();
	if (not device) {
		as.Destroy();
		return false;
	}

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
	if (sizes.ResultDataMaxSizeInBytes == 0) {
		logHandler.Print("AccelerationStructure: build size is zero\n");
		as.Destroy();
		return false;
	}

	if (not CreateDefaultBuffer(as.m_storage[0], sizes.ResultDataMaxSizeInBytes,
								D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "AccelerationStructure bottom level")) {
		logHandler.Print("AccelerationStructure: storage buffer allocation failed (%llu bytes)\n",
						 static_cast<unsigned long long>(sizes.ResultDataMaxSizeInBytes));
		as.Destroy();
		return false;
	}

	out.buildDesc.DestAccelerationStructureData = as.m_storage[0]->GetGPUVirtualAddress();
	as.m_storageSize[0] = sizes.ResultDataMaxSizeInBytes;
	out.scratchSize = sizes.ScratchDataSizeInBytes;
	return true;
}
}

// =================================================================================================

bool AccelerationStructure::BuildBottomLevelBatch(const AccelBuildItem* items, uint32_t itemCount)
noexcept
{
	if (not RayTracingApi::IsLoaded())
		return false;
	if ((not items) or (itemCount == 0))
		return false;

	const UINT64 alignment = UINT64(RayTracingApi::ScratchAlignment());

	std::vector<PreparedBuild>	prepared(itemCount);
	UINT64						scratchTotal = 0;
	uint32_t					nPrepared = 0;

	for (uint32_t i = 0; i < itemCount; ++i) {
		if (not items[i].structure)
			continue;
		PreparedBuild& p = prepared[nPrepared];
		if (not PrepareBottomLevel(*items[i].structure, items[i].geometries, items[i].geometryCount, p))
			continue;
		p.scratchOffset = scratchTotal;
		scratchTotal += (p.scratchSize + alignment - 1) & ~(alignment - 1);
		++nPrepared;
	}
	if (nPrepared == 0)
		return false;

	ComPtr<ID3D12Resource> scratch;
	if (not CreateDefaultBuffer(scratch, scratchTotal + alignment, D3D12_RESOURCE_STATE_COMMON, "AccelerationStructure scratch")) {
		logHandler.Print("AccelerationStructure: scratch allocation failed (%llu bytes)\n",
						 static_cast<unsigned long long>(scratchTotal));
		for (uint32_t i = 0; i < nPrepared; ++i)
			prepared[i].target->Destroy();
		return false;
	}
	D3D12_GPU_VIRTUAL_ADDRESS scratchBase = scratch->GetGPUVirtualAddress();
	scratchBase = (scratchBase + alignment - 1) & ~(alignment - 1);

	CommandList* cl = commandListHandler.CreateCmdList("AccelerationStructure::BuildBottomLevelBatch", true);
	if (not (cl and cl->Open())) {
		ReleaseBuffer(scratch);
		for (uint32_t i = 0; i < nPrepared; ++i)
			prepared[i].target->Destroy();
		return false;
	}

	ComPtr<ID3D12GraphicsCommandList4> list = RayTracingList(cl->GfxList());
	if (list) {
		for (uint32_t i = 0; i < nPrepared; ++i) {
			PreparedBuild& p = prepared[i];
			p.buildDesc.Inputs.pGeometryDescs = p.geoms.data();
			p.buildDesc.ScratchAccelerationStructureData = scratchBase + p.scratchOffset;
			list->BuildRaytracingAccelerationStructure(&p.buildDesc, 0, nullptr);
		}
	}
	cl->Flush();
	const bool submitted = (list != nullptr);
	ReleaseBuffer(scratch);

	for (uint32_t i = 0; i < nPrepared; ++i) {
		AccelerationStructure& as = *prepared[i].target;
		if (not submitted) {
			as.Destroy();
			continue;
		}
		as.m_address = as.m_storage[0]->GetGPUVirtualAddress();
		ReleaseUpload(as.m_vertices);
		ReleaseUpload(as.m_indices);
	}
	return submitted;
}

// =================================================================================================

bool AccelerationStructure::BuildBottomLevel(const AccelGeometryDesc* geometries, uint32_t geometryCount)
noexcept
{
	AccelBuildItem item;

	item.structure = this;
	item.geometries = geometries;
	item.geometryCount = geometryCount;
	return BuildBottomLevelBatch(&item, 1);
}

// =================================================================================================

bool AccelerationStructure::BuildTopLevel(const AccelInstance* instances, uint32_t instanceCount, bool immediate)
noexcept
{
	if (not RayTracingApi::IsLoaded())
		return false;
	if ((not instances) or (instanceCount == 0))
		return false;

	ComPtr<ID3D12Device5> device = RayTracingDevice();
	if (not device)
		return false;

	m_slot = uint32_t(commandListHandler.FrameIndex()) % kFrameSlots;

	ComPtr<ID3D12Resource>& instanceBuffer = m_instances[m_slot];

	if (instanceCount > m_instanceCapacity[m_slot]) {
		ReleaseUpload(instanceBuffer);
		instanceBuffer = gfxResourceHandler.AcquireUpload(size_t(instanceCount) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
		if (not instanceBuffer) {
			logHandler.Print("AccelerationStructure::BuildTopLevel: instance buffer allocation failed (%u)\n", instanceCount);
			m_instanceCapacity[m_slot] = 0;
			return false;
		}
		m_instanceCapacity[m_slot] = instanceCount;
	}

#if DBG_DIRECTX
	CheckInstanceBuffer(*this, instanceBuffer.Get());
#endif

	D3D12_RANGE	readRange{ 0, 0 };
	void*		instanceData = nullptr;
	if (FAILED(instanceBuffer->Map(0, &readRange, &instanceData)) or not instanceData) {
		logHandler.Print("AccelerationStructure::BuildTopLevel: instance buffer could not be mapped\n");
		return false;
	}
	D3D12_RAYTRACING_INSTANCE_DESC* pInstance = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(instanceData);

	uint32_t written = 0;
	for (uint32_t i = 0; i < instanceCount; ++i) {
		const AccelInstance& src = instances[i];
		if ((not src.blas) or (not src.blas->IsValid()))
			continue;
		D3D12_RAYTRACING_INSTANCE_DESC dst{};
		std::memcpy(dst.Transform, src.transform, sizeof(float) * 12);
		dst.InstanceID = src.customIndex & 0xFFFFFFu;
		dst.InstanceMask = src.mask & 0xFFu;
		dst.InstanceContributionToHitGroupIndex = 0;
		dst.Flags = src.singleSided ? UINT(D3D12_RAYTRACING_INSTANCE_FLAG_NONE)
									: UINT(D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE);
		dst.AccelerationStructure = src.blas->DeviceAddress();
		std::memcpy(pInstance + written, &dst, sizeof(dst));
		++written;
	}
	D3D12_RANGE writtenRange{ 0, SIZE_T(written) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC) };
	instanceBuffer->Unmap(0, &writtenRange);
	if (written == 0)
		return false;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC		buildDesc{};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS&	inputs = buildDesc.Inputs;
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.NumDescs = written;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.InstanceDescs = instanceBuffer->GetGPUVirtualAddress();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizes{};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &sizes);
	if (sizes.ResultDataMaxSizeInBytes == 0) {
		logHandler.Print("AccelerationStructure::BuildTopLevel: build size is zero\n");
		return false;
	}

	if ((not m_storage[m_slot]) or (m_storageSize[m_slot] < sizes.ResultDataMaxSizeInBytes)) {
		ReleaseBuffer(m_storage[m_slot]);
		m_storageSize[m_slot] = 0;
		if (not CreateDefaultBuffer(m_storage[m_slot], sizes.ResultDataMaxSizeInBytes,
									D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "AccelerationStructure top level")) {
			logHandler.Print("AccelerationStructure::BuildTopLevel: storage allocation failed (%llu bytes)\n",
							 static_cast<unsigned long long>(sizes.ResultDataMaxSizeInBytes));
			return false;
		}
		m_storageSize[m_slot] = sizes.ResultDataMaxSizeInBytes;
	}

	const UINT64 alignment = UINT64(RayTracingApi::ScratchAlignment());
	const UINT64 scratchNeeded = sizes.ScratchDataSizeInBytes + alignment;

	if (m_scratchSize[m_slot] < scratchNeeded) {
		ReleaseBuffer(m_scratch[m_slot]);
		m_scratchSize[m_slot] = 0;
		if (not CreateDefaultBuffer(m_scratch[m_slot], scratchNeeded, D3D12_RESOURCE_STATE_COMMON, "AccelerationStructure scratch")) {
			logHandler.Print("AccelerationStructure::BuildTopLevel: scratch allocation failed (%llu bytes)\n",
							 static_cast<unsigned long long>(scratchNeeded));
			return false;
		}
		m_scratchSize[m_slot] = scratchNeeded;
	}

	D3D12_GPU_VIRTUAL_ADDRESS scratchAddress = m_scratch[m_slot]->GetGPUVirtualAddress();
	scratchAddress = (scratchAddress + alignment - 1) & ~(alignment - 1);

	buildDesc.DestAccelerationStructureData = m_storage[m_slot]->GetGPUVirtualAddress();
	buildDesc.ScratchAccelerationStructureData = scratchAddress;

	ID3D12GraphicsCommandList*			frameList = immediate ? nullptr : commandListHandler.CurrentGfxList();
	ComPtr<ID3D12GraphicsCommandList7>	frameList7;

	if (frameList and g_enhancedBarriers)
		frameList->QueryInterface(IID_PPV_ARGS(frameList7.GetAddressOf()));
	if (frameList7) {
		ComPtr<ID3D12GraphicsCommandList4> list = RayTracingList(frameList);
		if (not list)
			return false;

		D3D12_RESOURCE_BARRIER before{};
		before.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		before.UAV.pResource = nullptr;
		list->ResourceBarrier(1, &before);
		list->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

		D3D12_BUFFER_BARRIER bufferBarrier{};
		bufferBarrier.SyncBefore = D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
		bufferBarrier.SyncAfter = D3D12_BARRIER_SYNC_ALL_SHADING;
		bufferBarrier.AccessBefore = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
		bufferBarrier.AccessAfter = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
		bufferBarrier.pResource = m_storage[m_slot].Get();
		bufferBarrier.Offset = 0;
		bufferBarrier.Size = UINT64_MAX;

		D3D12_BARRIER_GROUP barrierGroup{};
		barrierGroup.Type = D3D12_BARRIER_TYPE_BUFFER;
		barrierGroup.NumBarriers = 1;
		barrierGroup.pBufferBarriers = &bufferBarrier;
		frameList7->Barrier(1, &barrierGroup);
#if DBG_DIRECTX
		RecordTopLevelBuild(*this, *commandListHandler.CurrentCmdList(), false, instanceCount, buildDesc, sizes);
#endif
	}
	else {
		CommandList* cl = commandListHandler.CreateCmdList("AccelerationStructure::BuildTopLevel", true);
		if (not (cl and cl->Open()))
			return false;
		ComPtr<ID3D12GraphicsCommandList4> list = RayTracingList(cl->GfxList());
		if (list) {
			list->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
#if DBG_DIRECTX
			RecordTopLevelBuild(*this, *cl, true, instanceCount, buildDesc, sizes);
#endif
		}
		if (frameList)
			cl->Submit();
		else
			cl->Flush();
		if (not list)
			return false;
	}

	m_address = m_storage[m_slot]->GetGPUVirtualAddress();
	m_primitives = written;
	return true;
}

// =================================================================================================

#if DBG_DIRECTX
void AccelerationStructure::DumpTopLevelBuilds(void)
noexcept
{
	uint64_t count = (g_topLevelBuildCount < kTopLevelBuildRecords) ? g_topLevelBuildCount : kTopLevelBuildRecords;

	logHandler.Print("top level builds: %llu recorded, last %llu:\n", static_cast<unsigned long long>(g_topLevelBuildCount),
					 static_cast<unsigned long long>(count));
	for (uint64_t i = g_topLevelBuildCount - count; i < g_topLevelBuildCount; ++i) {
		const TopLevelBuildRecord& record = g_topLevelBuilds[i % kTopLevelBuildRecords];
		logHandler.Print("  build %llu: frame %llu, index %d, structure %p, slot %u, %s, list %s, instances %u written of %u, capacity %u, "
						 "result %llu of %llu bytes, scratch %llu of %llu bytes, storage VA 0x%llX, scratch VA 0x%llX, instance VA 0x%llX\n",
						 static_cast<unsigned long long>(i), static_cast<unsigned long long>(record.frameNumber), record.frameIndex,
						 static_cast<const void*>(record.structure), record.slot, record.immediate ? "immediate" : "in frame",
						 record.listName, record.written, record.instanceCount, record.capacity,
						 static_cast<unsigned long long>(record.resultSize), static_cast<unsigned long long>(record.storageSize),
						 static_cast<unsigned long long>(record.scratchNeeded), static_cast<unsigned long long>(record.scratchSize),
						 static_cast<unsigned long long>(record.storage), static_cast<unsigned long long>(record.scratch),
						 static_cast<unsigned long long>(record.instances));

		char	blasList[kTopLevelRecordDescs * 32];
		size_t	length = 0;

		blasList[0] = '\0';
		for (uint32_t j = 0; (j < record.descCount) and (length < sizeof(blasList)); ++j) {
			int added = std::snprintf(blasList + length, sizeof(blasList) - length, " 0x%llX/%u",
									  static_cast<unsigned long long>(record.descs[j].AccelerationStructure),
									  unsigned(record.descs[j].InstanceID));
			if (added < 0)
				break;
			length += size_t(added);
		}
		logHandler.Print("    recorded blas/id:%s\n", blasList);

		if (g_markers) {
			const volatile uint32_t* markers = g_markers + record.markerSlot * kMarkerFields;

			logHandler.Print("    gpu markers: build %s (marker %u, expected %u), draws %u recorded, %u started, %u completed\n",
							 (markers[0] == record.buildMarker) ? "completed" : "NOT completed", unsigned(markers[0]),
							 unsigned(record.buildMarker), record.drawsRecorded, unsigned(markers[1]), unsigned(markers[2]));
		}

		if (record.released) {
			logHandler.Print("    instance buffer released since\n");
			continue;
		}

		D3D12_RAYTRACING_INSTANCE_DESC	current[kTopLevelRecordDescs];
		HRESULT							hr = S_OK;

		if (not ReadInstanceDescs(record.instanceBuffer, record.descCount, current, hr)) {
			logHandler.Print("    instance buffer could not be mapped (0x%08X)\n", unsigned(hr));
			continue;
		}

		uint32_t index = FirstChangedDesc(record, current);

		if (index == record.descCount)
			logHandler.Print("    instance buffer unchanged (%u descs compared)\n", record.descCount);
		else
			PrintChangedDesc("   ", i, record, index, current[index]);
	}
}


void AccelerationStructure::MarkTraceDraw(void)
noexcept
{
	if ((g_topLevelBuildCount == 0) or not g_markers)
		return;

	TopLevelBuildRecord&	record = g_topLevelBuilds[(g_topLevelBuildCount - 1) % kTopLevelBuildRecords];
	CommandList*			cl = commandListHandler.CurrentCmdList();

	if ((cl == nullptr) or (cl != record.list) or (cl->GetExecutionCounter() != record.listExecution))
		return;
	++record.drawsRecorded;

	const uint32_t							fields[2] = { 1, 2 };
	const uint32_t							values[2] = { record.drawsRecorded, record.drawsRecorded };
	const D3D12_WRITEBUFFERIMMEDIATE_MODE	modes[2] = { D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN, D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT };

	WriteMarkers(cl->GfxList(), record.markerSlot, fields, values, modes, 2);
}
#endif

// =================================================================================================

void AccelerationStructure::Move(AccelerationStructure& other)
noexcept
{
	m_vertices = std::move(other.m_vertices);
	m_indices = std::move(other.m_indices);
	m_address = other.m_address;
	m_primitives = other.m_primitives;
	m_slot = other.m_slot;
	for (uint32_t i = 0; i < kFrameSlots; ++i) {
		m_storage[i] = std::move(other.m_storage[i]);
		m_storageSize[i] = other.m_storageSize[i];
		m_instances[i] = std::move(other.m_instances[i]);
		m_instanceCapacity[i] = other.m_instanceCapacity[i];
		m_scratch[i] = std::move(other.m_scratch[i]);
		m_scratchSize[i] = other.m_scratchSize[i];

		other.m_storageSize[i] = 0;
		other.m_instanceCapacity[i] = 0;
		other.m_scratchSize[i] = 0;
	}

	other.m_address = 0;
	other.m_primitives = 0;
	other.m_slot = 0;
}


bool AccelerationStructure::Bind(void) const
noexcept
{
	if (not IsValid())
		return false;
	commandListHandler.BindAccelerationStructure(Handle());
	return true;
}


void AccelerationStructure::Destroy(void)
noexcept
{
	for (uint32_t i = 0; i < kFrameSlots; ++i) {
		ReleaseBuffer(m_storage[i]);
		m_storageSize[i] = 0;
		ReleaseUpload(m_instances[i]);
		m_instanceCapacity[i] = 0;
		ReleaseBuffer(m_scratch[i]);
		m_scratchSize[i] = 0;
	}
	ReleaseUpload(m_vertices);
	ReleaseUpload(m_indices);
	m_slot = 0;
	m_address = 0;
	m_primitives = 0;
}

// =================================================================================================
