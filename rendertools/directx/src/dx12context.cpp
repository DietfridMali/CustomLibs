#include "dx12context.h"
#include "commandlist.h"
#include "shader.h"
#include "acceleration_structure.h"
#include "xrhandler.h"
#include "loghandler.h"

#include <cstdio>
#include <memory>
#include <chrono>

#if USE_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"

#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "GFSDK_Aftermath_Lib.x64.lib")

namespace {
constexpr int	kGpuCrashDumpTimeout = 10000;
constexpr int	kGpuCrashDumpPoll = 50;

std::mutex		g_gpuCrashDumpMutex;
bool			g_gpuCrashDumpsEnabled{ false };

struct ShaderBinary {
	char					name[128]{};
	std::vector<uint8_t>	code;
};

std::vector<uint8_t>											g_gpuCrashDump;
std::mutex														g_shaderDataMutex;
std::map<std::pair<uint64_t, uint64_t>, std::vector<uint8_t>>	g_shaderDebugInfos;
std::unordered_map<uint64_t, ShaderBinary>						g_shaderBinaries;

void GpuCrashDumpPath(char* path, size_t size, const char* fileName)
noexcept
{
	const char*	logPath = logHandler.Path();
	const char*	separator = std::strrchr(logPath, '/');
	const char*	backslash = std::strrchr(logPath, '\\');

	if ((backslash != nullptr) and ((separator == nullptr) or (backslash > separator)))
		separator = backslash;

	int folderLength = (separator != nullptr) ? int(separator - logPath) + 1 : 0;

	std::snprintf(path, size, "%.*s%s", folderLength, logPath, fileName);
}


void WriteGpuCrashDumpFile(const char* fileName, const void* data, size_t size)
noexcept
{
	char path[LogHandler::kPathSize];

	GpuCrashDumpPath(path, sizeof(path), fileName);

	FILE* file = std::fopen(path, "wb");

	if (not file) {
		logHandler.Print("Aftermath: %s could not be opened\n", path);
		return;
	}

	size_t written = std::fwrite(data, 1, size, file);

	std::fclose(file);
	logHandler.Print("Aftermath: %s, %zu of %zu bytes written\n", path, written, size);
}


void GFSDK_AFTERMATH_CALL ShaderDebugInfoCallback(const void* shaderDebugInfo, const uint32_t shaderDebugInfoSize, void*)
noexcept
{
	GFSDK_Aftermath_ShaderDebugInfoIdentifier	identifier{};
	GFSDK_Aftermath_Result						result =
		GFSDK_Aftermath_GetShaderDebugInfoIdentifier(GFSDK_Aftermath_Version_API, shaderDebugInfo, shaderDebugInfoSize, &identifier);

	if (not GFSDK_Aftermath_SUCCEED(result)) {
		logHandler.Print("Aftermath: shader debug info without identifier (0x%08X)\n", unsigned(result));
		return;
	}

	const uint8_t*					data = static_cast<const uint8_t*>(shaderDebugInfo);
	std::lock_guard<std::mutex>	lock(g_shaderDataMutex);

	g_shaderDebugInfos[std::make_pair(identifier.id[0], identifier.id[1])].assign(data, data + shaderDebugInfoSize);
}


void GFSDK_AFTERMATH_CALL ShaderDebugInfoLookupCallback(const GFSDK_Aftermath_ShaderDebugInfoIdentifier* identifier,
														PFN_GFSDK_Aftermath_SetData setShaderDebugInfo, void*)
noexcept
{
	std::lock_guard<std::mutex>	lock(g_shaderDataMutex);
	auto							entry = g_shaderDebugInfos.find(std::make_pair(identifier->id[0], identifier->id[1]));

	if (entry == g_shaderDebugInfos.end()) {
		logHandler.Print("Aftermath: no shader debug info for 0x%016llX%016llX\n", static_cast<unsigned long long>(identifier->id[0]),
						 static_cast<unsigned long long>(identifier->id[1]));
		return;
	}
	setShaderDebugInfo(entry->second.data(), uint32_t(entry->second.size()));
}


void GFSDK_AFTERMATH_CALL ShaderLookupCallback(const GFSDK_Aftermath_ShaderBinaryHash* shaderHash, PFN_GFSDK_Aftermath_SetData setShaderBinary,
											   void*)
noexcept
{
	std::lock_guard<std::mutex>	lock(g_shaderDataMutex);
	auto							entry = g_shaderBinaries.find(shaderHash->hash);

	if (entry == g_shaderBinaries.end()) {
		logHandler.Print("Aftermath: no shader binary for hash 0x%016llX\n", static_cast<unsigned long long>(shaderHash->hash));
		return;
	}
	setShaderBinary(entry->second.code.data(), uint32_t(entry->second.code.size()));
}


const char* ShaderBinaryName(uint64_t hash)
noexcept
{
	std::lock_guard<std::mutex>	lock(g_shaderDataMutex);
	auto							entry = g_shaderBinaries.find(hash);

	return (entry == g_shaderBinaries.end()) ? "unknown" : entry->second.name;
}


void WriteGpuCrashDumpJSON(GFSDK_Aftermath_GpuCrashDump_Decoder decoder)
noexcept
{
	uint32_t				jsonSize = 0;
	GFSDK_Aftermath_Result	result = GFSDK_Aftermath_GpuCrashDump_GenerateJSON(decoder, GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO,
																			   GFSDK_Aftermath_GpuCrashDumpFormatterFlags_NONE,
																			   ShaderDebugInfoLookupCallback, ShaderLookupCallback, nullptr,
																			   nullptr, &jsonSize);

	if (not GFSDK_Aftermath_SUCCEED(result) or (result == GFSDK_Aftermath_Result_NotAvailable) or (jsonSize == 0)) {
		logHandler.Print("Aftermath: crash dump JSON could not be generated (0x%08X, %u bytes)\n", unsigned(result), jsonSize);
		return;
	}

	std::vector<char> json(jsonSize);

	result = GFSDK_Aftermath_GpuCrashDump_GetJSON(decoder, jsonSize, json.data());
	if (not GFSDK_Aftermath_SUCCEED(result))
		logHandler.Print("Aftermath: crash dump JSON could not be read (0x%08X)\n", unsigned(result));
	else
		WriteGpuCrashDumpFile("gpucrash.json", json.data(), strnlen(json.data(), json.size()));
}


void LogActiveShaders(GFSDK_Aftermath_GpuCrashDump_Decoder decoder)
noexcept
{
	uint32_t				shaderCount = 0;
	GFSDK_Aftermath_Result	result = GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfoCount(decoder, &shaderCount);

	if (not GFSDK_Aftermath_SUCCEED(result) or (result == GFSDK_Aftermath_Result_NotAvailable) or (shaderCount == 0)) {
		logHandler.Print("Aftermath: no active shaders in the crash dump (0x%08X)\n", unsigned(result));
		return;
	}

	std::vector<GFSDK_Aftermath_GpuCrashDump_ShaderInfo> shaderInfos(shaderCount);

	result = GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfo(decoder, shaderCount, shaderInfos.data());
	if (not GFSDK_Aftermath_SUCCEED(result)) {
		logHandler.Print("Aftermath: active shaders could not be read (0x%08X)\n", unsigned(result));
		return;
	}
	for (const GFSDK_Aftermath_GpuCrashDump_ShaderInfo& shaderInfo : shaderInfos) {
		GFSDK_Aftermath_ShaderBinaryHash binaryHash{};

		result = GFSDK_Aftermath_GetShaderHashForShaderInfo(decoder, &shaderInfo, &binaryHash);
		if (GFSDK_Aftermath_SUCCEED(result))
			logHandler.Print("Aftermath: active shader '%s', type %u, internal %d, dump hash %llu, hash 0x%016llX\n",
							 ShaderBinaryName(binaryHash.hash), unsigned(shaderInfo.shaderType), int(shaderInfo.isInternal),
							 static_cast<unsigned long long>(shaderInfo.shaderHash), static_cast<unsigned long long>(binaryHash.hash));
		else
			logHandler.Print("Aftermath: active shader, type %u, internal %d, dump hash %llu, hash not available (0x%08X)\n",
							 unsigned(shaderInfo.shaderType), int(shaderInfo.isInternal), static_cast<unsigned long long>(shaderInfo.shaderHash),
							 unsigned(result));
	}
}


void GFSDK_AFTERMATH_CALL GpuCrashDumpCallback(const void* gpuCrashDump, const uint32_t gpuCrashDumpSize, void*)
noexcept
{
	std::lock_guard<std::mutex>	lock(g_gpuCrashDumpMutex);
	const uint8_t*				data = static_cast<const uint8_t*>(gpuCrashDump);

	WriteGpuCrashDumpFile("gpucrash.nv-gpudmp", gpuCrashDump, gpuCrashDumpSize);
	g_gpuCrashDump.assign(data, data + gpuCrashDumpSize);
}


void DecodeGpuCrashDump(void)
noexcept
{
	std::lock_guard<std::mutex> lock(g_gpuCrashDumpMutex);

	if (g_gpuCrashDump.empty()) {
		logHandler.Print("Aftermath: no crash dump received\n");
		return;
	}

	GFSDK_Aftermath_GpuCrashDump_Decoder	decoder{};
	GFSDK_Aftermath_Result					result = GFSDK_Aftermath_GpuCrashDump_CreateDecoder(
		GFSDK_Aftermath_Version_API, g_gpuCrashDump.data(), uint32_t(g_gpuCrashDump.size()), &decoder);

	if (not GFSDK_Aftermath_SUCCEED(result)) {
		logHandler.Print("Aftermath: crash dump decoder could not be created (0x%08X)\n", unsigned(result));
		return;
	}
	{
		std::lock_guard<std::mutex> shaderLock(g_shaderDataMutex);

		logHandler.Print("Aftermath: %zu shader debug infos, %zu shader binaries kept\n", g_shaderDebugInfos.size(), g_shaderBinaries.size());
	}
	WriteGpuCrashDumpJSON(decoder);
	LogActiveShaders(decoder);
	GFSDK_Aftermath_GpuCrashDump_DestroyDecoder(decoder);
}
}
#endif

// =================================================================================================

bool DX12Context::SelectAdapter(void)
noexcept
{
	if (xrHandler.IsAvailable()) {
		D3D_FEATURE_LEVEL minFeatureLevel = m_featureLevel;

		if (not xrHandler.GetD3D12Adapter(m_factory.Get(), m_adapter, minFeatureLevel))
			return false;
		if (minFeatureLevel > m_featureLevel)
			m_featureLevel = minFeatureLevel;
		return true;
	}

	UINT					adapterIndex = 0;
	ComPtr<IDXGIAdapter1>	adapter;
	SIZE_T					maxVRAM = 0;

	while (m_factory->EnumAdapters1(adapterIndex++, &adapter) != DXGI_ERROR_NOT_FOUND) {
		DXGI_ADAPTER_DESC1 desc{};
		adapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;
		if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
			continue;
		if (desc.DedicatedVideoMemory > maxVRAM) {
			maxVRAM = desc.DedicatedVideoMemory;
			m_adapter = adapter;
		}
	}
	return m_adapter != nullptr;
}


bool DX12Context::Create(bool enableDebugLayer, const GfxFeatureRequest& request)
noexcept
{
#if USE_AFTERMATH
	GFSDK_Aftermath_Result aftermathResult;

	if (not g_gpuCrashDumpsEnabled) {
		aftermathResult = GFSDK_Aftermath_EnableGpuCrashDumps(GFSDK_Aftermath_Version_API, GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
															  GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks, GpuCrashDumpCallback,
															  ShaderDebugInfoCallback, nullptr, nullptr, nullptr);
		g_gpuCrashDumpsEnabled = GFSDK_Aftermath_SUCCEED(aftermathResult);
		logHandler.Print("Aftermath: GPU crash dumps %s (0x%08X)\n", g_gpuCrashDumpsEnabled ? "enabled" : "not enabled",
						 unsigned(aftermathResult));
	}
#endif
#if DBG_DIRECTX
	if (enableDebugLayer) {
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugController))))
			m_debugController->EnableDebugLayer();

		// DRED: auto-breadcrumbs track which GPU op was in flight when the device hung.
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
			dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		}
	}
#endif

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&m_factory));
	if (FAILED(hr)) {
		logHandler.Print("DX12Context: Failed to create DXGI factory (hr=0x%08X)\n", (unsigned)hr);
		return false;
	}

	if (not SelectAdapter()) {
		logHandler.Print("DX12Context: No suitable DX12 adapter found\n");
		return false;
	}

	hr = xrHandler.IsAvailable() ? (xrHandler.CreateD3D12Device(m_adapter.Get(), m_featureLevel, m_device) ? S_OK : E_FAIL)
								 : D3D12CreateDevice(m_adapter.Get(), m_featureLevel, IID_PPV_ARGS(&m_device));
	if (FAILED(hr)) {
		logHandler.Print("DX12Context: Failed to create D3D12 device (hr=0x%08X)\n", (unsigned)hr);
		return false;
	}

#if USE_AFTERMATH
	if (g_gpuCrashDumpsEnabled) {
		aftermathResult = GFSDK_Aftermath_DX12_Initialize(GFSDK_Aftermath_Version_API,
														  GFSDK_Aftermath_FeatureFlags_EnableMarkers |
															  GFSDK_Aftermath_FeatureFlags_EnableResourceTracking |
															  GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo,
														  m_device.Get());
		logHandler.Print("Aftermath: DX12 device %s (0x%08X)\n", GFSDK_Aftermath_SUCCEED(aftermathResult) ? "initialized" : "not initialized",
						 unsigned(aftermathResult));
	}
#endif

#if DBG_DIRECTX
	if (enableDebugLayer) {
		if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&m_infoQueue)))) {
			// All severity breaks disabled — DrainMessages() after each Execute prints everything.
			m_infoQueue->SetBreakOnID(D3D12_MESSAGE_ID(209), TRUE); // vertex buffer stride < input layout stride
			m_infoQueue->SetBreakOnID(D3D12_MESSAGE_ID(210), TRUE); // vertex buffer too small for draw call
		}
	}
#endif

	m_hasRayTracing = ((request.Requested() & GfxFeatureBit(GfxFeature::RayTracing)) != 0) and SupportsRayTracing() and
		Shader::SupportsRayQuery();
	if (m_hasRayTracing and not RayTracingApi::Load(m_device.Get()))
		m_hasRayTracing = false;
#ifdef _DEBUG
	logHandler.Print("DirectX ray tracing: %s\n", m_hasRayTracing ? "available (ray query)" : "not available");
#endif
	m_hasBindlessTextures = SupportsBindlessTextures();
#ifdef _DEBUG
	logHandler.Print("DirectX bindless textures: %s\n", m_hasBindlessTextures ? "available" : "not available");
#endif
	m_hasMultiview = SupportsMultiview();
	return true;
}


void DX12Context::Destroy(void)
noexcept
{
#if DBG_DIRECTX
	m_infoQueue.Reset();
	m_debugController.Reset();
#endif
	m_device.Reset();
	m_adapter.Reset();
	m_factory.Reset();
	m_featureLevel = D3D_FEATURE_LEVEL_12_0;
	m_hasRayTracing = false;
	m_hasBindlessTextures = false;
	m_hasMultiview = false;
}


bool DX12Context::SupportsRayTracing(void)
noexcept
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
		return false;
	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
		return false;

	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{ D3D_SHADER_MODEL_6_5 };
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
		return false;
	return shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_5;
}


bool DX12Context::SupportsBindlessTextures(void)
noexcept
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
		return false;
	return options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2;
}


bool DX12Context::SupportsMultiview(void)
noexcept
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3{};
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))))
		return false;
	if (options3.ViewInstancingTier < D3D12_VIEW_INSTANCING_TIER_1)
		return false;

	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{ D3D_SHADER_MODEL_6_1 };
	if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
		return false;
	return shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_1;
}

// =================================================================================================

#if DBG_DIRECTX
void DX12Context::DumpDRED(void)
noexcept
{
	if (not m_device)
		return;
	ComPtr<ID3D12DeviceRemovedExtendedData> dred;
	if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&dred))))
		return;

	D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc{};
	if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc))) {
		const D3D12_AUTO_BREADCRUMB_NODE* node = bc.pHeadAutoBreadcrumbNode;
		for (int n = 0; node; node = node->pNext, ++n) {
			const char*	clName = node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "(unnamed)";
			const char*	cqName = node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "(unnamed)";
			UINT		last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
			logHandler.Print("DRED node %d: CL=%s CQ=%s  ops=%u last=%u\n",
							 n, clName, cqName, node->BreadcrumbCount, last);
			UINT from = (last > 3 ? last - 3 : 0);
			bool incomplete = last < node->BreadcrumbCount;
			for (UINT i = from; (i < node->BreadcrumbCount) and (i < last + 11); ++i) {
				const char* state = "";
				if (incomplete) {
					if (i + 1 == last)
						state = " <-- last completed";
					else if (i == last)
						state = " <-- first not completed";
					else if (i > last)
						state = " <-- pending";
				}
				logHandler.Print("  [%u] op=%u%s\n", i, unsigned(node->pCommandHistory[i]), state);
			}
		}
	}

	D3D12_DRED_PAGE_FAULT_OUTPUT pf{};
	if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) && pf.PageFaultVA != 0)
		logHandler.Print("DRED page fault VA=0x%llX\n", (unsigned long long)pf.PageFaultVA);
	AccelerationStructure::DumpTopLevelBuilds();
}
#endif


#if USE_AFTERMATH
void DX12Context::WaitForGpuCrashDump(void)
noexcept
{
	if (not g_gpuCrashDumpsEnabled)
		return;

	GFSDK_Aftermath_CrashDump_Status	status = GFSDK_Aftermath_CrashDump_Status_Unknown;
	int									elapsed = 0;

	GFSDK_Aftermath_GetCrashDumpStatus(&status);
	while ((status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed) and (status != GFSDK_Aftermath_CrashDump_Status_Finished) and
		   (elapsed < kGpuCrashDumpTimeout)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(kGpuCrashDumpPoll));
		elapsed += kGpuCrashDumpPoll;
		GFSDK_Aftermath_GetCrashDumpStatus(&status);
	}
	logHandler.Print("Aftermath: crash dump status %u after %d ms\n", unsigned(status), elapsed);
	DecodeGpuCrashDump();
}


void DX12Context::LogShaderHash(const char* shaderName, const char* stage, const void* code, size_t codeSize)
noexcept
{
	D3D12_SHADER_BYTECODE				bytecode{ code, codeSize };
	GFSDK_Aftermath_ShaderBinaryHash	hash{};
	GFSDK_Aftermath_Result				result = GFSDK_Aftermath_GetShaderHash(GFSDK_Aftermath_Version_API, &bytecode, &hash);

	if (not GFSDK_Aftermath_SUCCEED(result)) {
		logHandler.Print("Aftermath: shader '%s' %s, hash not available (0x%08X)\n", shaderName, stage, unsigned(result));
		return;
	}

	const uint8_t*					data = static_cast<const uint8_t*>(code);
	std::lock_guard<std::mutex>	lock(g_shaderDataMutex);
	ShaderBinary&					binary = g_shaderBinaries[hash.hash];

	std::snprintf(binary.name, sizeof(binary.name), "%s %s", shaderName, stage);
	binary.code.assign(data, data + codeSize);
}
#endif


#if DBG_DIRECTX
void DX12Context::QueryVRAM(void)
noexcept
{
#if 0
    // Throttle to ~1/s — querying per frame is cheap, printing per frame spams.
    static std::chrono::steady_clock::time_point lastPrint{};
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPrint).count() < 1000)
        return;

    ComPtr<IDXGIAdapter3> adapter;
    if (not m_adapter or FAILED(m_adapter.As(&adapter)))
        return;
    DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonLocal{};
    if (FAILED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)))
        return;
    adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal);
    lastPrint = now;
    const double toMB = 1.0 / (1024.0 * 1024.0);
    logHandler.Print("VRAM local: usage=%.0f / budget=%.0f MB%s | nonlocal usage=%.0f MB\n",
        double(local.CurrentUsage) * toMB, double(local.Budget) * toMB,
        (local.CurrentUsage > local.Budget) ? "  <-- OVER BUDGET" : "",
        double(nonLocal.CurrentUsage) * toMB);
#endif
}


int DX12Context::DrainMessages(bool onlyErrors)
noexcept
{
	if (not m_infoQueue)
		return 0;
	UINT64	count = m_infoQueue->GetNumStoredMessages();
	int		errorCount = 0;
	for (UINT64 i = 0; i < count; ++i) {
		// In Unicode builds the preprocessor expands GetMessage → GetMessageW
		// when d3d12sdklayers.h is parsed, so the vtable entry is named GetMessageW.
		SIZE_T len = 0;
		m_infoQueue->GetMessageW(i, nullptr, &len);
		if (len == 0)
			continue;
		auto			buf = std::make_unique<char[]>(len);
		D3D12_MESSAGE*	msg = reinterpret_cast<D3D12_MESSAGE*>(buf.get());
		m_infoQueue->GetMessageW(i, msg, &len);
		const char* sev = "INFO";
		if (msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
			++errorCount;
			sev = "CORRUPTION";
		}
		else if (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
			++errorCount;
			sev = "ERROR";
		}
		else if (msg->Severity == D3D12_MESSAGE_SEVERITY_WARNING)
			sev = "WARNING";
		CommandList* cl = commandListHandler.CurrentCmdList();
		// split to make debugging real errors easier
#if DBG_DIRECTX
		if ((msg->Severity == D3D12_MESSAGE_SEVERITY_INFO) or (msg->Severity == D3D12_MESSAGE_SEVERITY_WARNING)) {
			if (not onlyErrors and (unsigned) msg->ID != 820)
				logHandler.Print("D3D12 %s (id=%u) [CL: %s]: %s\n", sev, (unsigned)msg->ID,
								 cl ? (const char*)cl->GetName() : "(none)", msg->pDescription);
		}
		else
			logHandler.Print("D3D12 %s (id=%u) [CL: %s]: %s\n", sev, (unsigned)msg->ID,
							 cl ? (const char*)cl->GetName() : "(none)", msg->pDescription);
#endif
	}
	m_infoQueue->ClearStoredMessages();
	return errorCount;
}
#endif

// =================================================================================================
