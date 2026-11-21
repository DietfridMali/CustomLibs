#include "vkframework.h"
#include "device_diagnostics.h"
#include "vkcontext.h"
#include "commandlist.h"
#include "gfxstates.h"
#include "loghandler.h"

#include <cstdio>
#include <cstring>
#include <vector>

// =================================================================================================

namespace {

const char* KindText(DeviceDiagnostics::MarkKind kind)
noexcept
{
	switch (kind) {
		case DeviceDiagnostics::MarkKind::ListBegin:
			return "list begin";
		case DeviceDiagnostics::MarkKind::ListEnd:
			return "list end";
		case DeviceDiagnostics::MarkKind::Stage:
			return "stage";
		case DeviceDiagnostics::MarkKind::Pipeline:
			return "pipeline";
		case DeviceDiagnostics::MarkKind::Compute:
			return "compute";
		case DeviceDiagnostics::MarkKind::OneShotBegin:
			return "one-shot begin";
		case DeviceDiagnostics::MarkKind::OneShotEnd:
			return "one-shot end";
		case DeviceDiagnostics::MarkKind::Draw:
			return "draw";
	}
	return "?";
}


const char* StageText(VkPipelineStageFlagBits stage)
noexcept
{
	switch (stage) {
		case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
			return "top of pipe";
		case VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT:
			return "draw indirect";
		case VK_PIPELINE_STAGE_VERTEX_INPUT_BIT:
			return "vertex input";
		case VK_PIPELINE_STAGE_VERTEX_SHADER_BIT:
			return "vertex shader";
		case VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT:
			return "tessellation control shader";
		case VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT:
			return "tessellation evaluation shader";
		case VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT:
			return "geometry shader";
		case VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
			return "fragment shader";
		case VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT:
			return "early fragment tests";
		case VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT:
			return "late fragment tests";
		case VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:
			return "color attachment output";
		case VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:
			return "compute shader";
		case VK_PIPELINE_STAGE_TRANSFER_BIT:
			return "transfer";
		case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:
			return "bottom of pipe";
		case VK_PIPELINE_STAGE_HOST_BIT:
			return "host";
		case VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT:
			return "all graphics";
		case VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:
			return "all commands";
		default:
			return nullptr;
	}
}


const char* AddressTypeText(VkDeviceFaultAddressTypeEXT type)
noexcept
{
	switch (type) {
		case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT:
			return "none";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:
			return "invalid read";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:
			return "invalid write";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:
			return "invalid execute";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:
			return "instruction pointer (unknown)";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:
			return "instruction pointer (invalid)";
		case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:
			return "instruction pointer (fault)";
		default:
			return "?";
	}
}

} // namespace

// =================================================================================================

void DeviceDiagnostics::Select(VkPhysicalDevice physicalDevice, bool hasFaultExtension, bool hasCheckpointExtension, bool hasBufferMarkerExtension)
noexcept
{
	m_faultFeatures = VkPhysicalDeviceFaultFeaturesEXT{};
	m_faultFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
	m_hasFault = false;
	m_hasFaultBinary = false;
	if (hasFaultExtension) {
		VkPhysicalDeviceFaultFeaturesEXT supported{};
		supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
		VkPhysicalDeviceFeatures2 features{};
		features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features.pNext = &supported;
		vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
		m_hasFault = supported.deviceFault == VK_TRUE;
		m_hasFaultBinary = m_hasFault and (supported.deviceFaultVendorBinary == VK_TRUE);
		m_faultFeatures.deviceFault = m_hasFault ? VK_TRUE : VK_FALSE;
		m_faultFeatures.deviceFaultVendorBinary = m_hasFaultBinary ? VK_TRUE : VK_FALSE;
	}
	m_hasCheckpoints = hasCheckpointExtension;
	m_hasBufferMarker = hasBufferMarkerExtension;
}


bool DeviceDiagnostics::Load(VkDevice device)
noexcept
{
	if (m_hasFault) {
		m_pfnGetDeviceFaultInfo = reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(vkGetDeviceProcAddr(device, "vkGetDeviceFaultInfoEXT"));
		m_hasFault = m_pfnGetDeviceFaultInfo != nullptr;
	}
	if (m_hasCheckpoints) {
		m_pfnCmdSetCheckpoint = reinterpret_cast<PFN_vkCmdSetCheckpointNV>(vkGetDeviceProcAddr(device, "vkCmdSetCheckpointNV"));
		m_pfnGetQueueCheckpointData = reinterpret_cast<PFN_vkGetQueueCheckpointDataNV>(vkGetDeviceProcAddr(device, "vkGetQueueCheckpointDataNV"));
		m_hasCheckpoints = (m_pfnCmdSetCheckpoint != nullptr) and (m_pfnGetQueueCheckpointData != nullptr);
	}
	if (m_hasBufferMarker) {
		m_pfnCmdWriteBufferMarker = reinterpret_cast<PFN_vkCmdWriteBufferMarkerAMD>(vkGetDeviceProcAddr(device, "vkCmdWriteBufferMarkerAMD"));
		m_hasBufferMarker = m_pfnCmdWriteBufferMarker != nullptr;
	}
	logHandler.Print("Vulkan device lost diagnostics: fault info %s%s, checkpoints %s, buffer markers %s\n", m_hasFault ? "on" : "off",
					 m_hasFaultBinary ? " (with vendor binary)" : "", m_hasCheckpoints ? "on" : "off", m_hasBufferMarker ? "on" : "off");
	return true;
}


bool DeviceDiagnostics::Create(VmaAllocator allocator)
noexcept
{
	if (not m_hasBufferMarker)
		return true;
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = 4 * sizeof(uint32_t);
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VmaAllocationInfo info{};
	VkResult res = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_markerBuffer, &m_markerAllocation, &info);
	if ((res != VK_SUCCESS) or (info.pMappedData == nullptr)) {
		logHandler.Print("DeviceDiagnostics::Create: marker buffer failed (%d), buffer markers off\n", int(res));
		if (m_markerBuffer != VK_NULL_HANDLE)
			vmaDestroyBuffer(allocator, m_markerBuffer, m_markerAllocation);
		m_markerBuffer = VK_NULL_HANDLE;
		m_markerAllocation = VK_NULL_HANDLE;
		m_hasBufferMarker = false;
		return false;
	}
	m_markerData = static_cast<volatile uint32_t*>(info.pMappedData);
	for (uint32_t i = 0; i < 4; ++i)
		m_markerData[i] = 0;
	return true;
}


void DeviceDiagnostics::Destroy(VmaAllocator allocator)
noexcept
{
	if ((m_markerBuffer != VK_NULL_HANDLE) and (allocator != VK_NULL_HANDLE))
		vmaDestroyBuffer(allocator, m_markerBuffer, m_markerAllocation);
	m_markerBuffer = VK_NULL_HANDLE;
	m_markerAllocation = VK_NULL_HANDLE;
	m_markerData = nullptr;
	m_hasBufferMarker = false;
	m_hasCheckpoints = false;
	m_hasFault = false;
	m_hasFaultBinary = false;
}


void DeviceDiagnostics::Mark(VkCommandBuffer cb, MarkKind kind, const char* name)
noexcept
{
	if (cb == VK_NULL_HANDLE)
		return;
	uint32_t	serial = m_serial.fetch_add(1) + 1;
	Record&		record = m_records[serial % kRingSize];
	record.serial = serial;
	record.kind = kind;
	record.frame = commandListHandler.CmdQueue().FrameNumber();
	snprintf(record.name, kNameSize, "%s", name ? name : "");
	if (m_hasCheckpoints)
		m_pfnCmdSetCheckpoint(cb, reinterpret_cast<const void*>(static_cast<uintptr_t>(serial)));
	if (m_hasBufferMarker) {
		m_pfnCmdWriteBufferMarker(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_markerBuffer, 0, serial);
		m_pfnCmdWriteBufferMarker(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_markerBuffer, sizeof(uint32_t), serial);
	}
}


void DeviceDiagnostics::FormatMark(uint32_t serial, char* text, size_t size) const
noexcept
{
	if (serial == 0) {
		snprintf(text, size, "none");
		return;
	}
	const Record& record = m_records[serial % kRingSize];
	if (record.serial != serial) {
		snprintf(text, size, "mark %u (no longer in the record ring)", serial);
		return;
	}
	snprintf(text, size, "mark %u, frame %llu, %s '%s'", serial, static_cast<unsigned long long>(record.frame), KindText(record.kind), record.name);
}


void DeviceDiagnostics::NoteGpuMark(uint32_t serial)
noexcept
{
	if (serial == 0)
		return;
	if ((m_gpuLow == 0) or (serial < m_gpuLow))
		m_gpuLow = serial;
	if (serial > m_gpuHigh)
		m_gpuHigh = serial;
}


void DeviceDiagnostics::ReportGpuWindow(void)
noexcept
{
	if (m_gpuHigh == 0) {
		logHandler.Print("  marks around the GPU position: no GPU position known\n");
		return;
	}
	uint32_t last = m_serial.load();
	uint32_t first = (m_gpuLow > kWindowContext) ? m_gpuLow - kWindowContext : 1;
	uint32_t end = ((last - m_gpuHigh) > kWindowContext) ? m_gpuHigh + kWindowContext : last;
	if (end - first + 1 > kWindowMax)
		end = first + kWindowMax - 1;
	logHandler.Print("  marks around the GPU position (%u..%u, '>' = between last finished and last started):\n", first, end);
	for (uint32_t serial = first; serial <= end; ++serial) {
		char text[kTextSize];
		FormatMark(serial, text, sizeof(text));
		logHandler.Print("  %c %s\n", ((serial >= m_gpuLow) and (serial <= m_gpuHigh)) ? '>' : ' ', text);
	}
}


void DeviceDiagnostics::Report(void)
noexcept
{
	bool expected = false;
	if (not m_isReported.compare_exchange_strong(expected, true))
		return;
	logHandler.Print("device lost report: frame %llu, last recorded mark %u\n",
					 static_cast<unsigned long long>(commandListHandler.CmdQueue().FrameNumber()), m_serial.load());
	ReportFault();
	ReportCheckpoints();
	ReportBufferMarkers();
	ReportGpuWindow();
	ReportRecentMarks();
	gfxStates.LogMemoryUsage("device lost");
}


void DeviceDiagnostics::ReportFault(void)
noexcept
{
	if (not m_hasFault) {
		logHandler.Print("  device fault info: not available on this device\n");
		return;
	}
	VkDevice				device = vkContext.Device();
	VkDeviceFaultCountsEXT	counts{};
	counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
	VkResult res = m_pfnGetDeviceFaultInfo(device, &counts, nullptr);
	if (res != VK_SUCCESS) {
		logHandler.Print("  device fault info: vkGetDeviceFaultInfoEXT (counts) failed (%d)\n", int(res));
		return;
	}
	std::vector<VkDeviceFaultAddressInfoEXT>	addresses(counts.addressInfoCount);
	std::vector<VkDeviceFaultVendorInfoEXT>		vendors(counts.vendorInfoCount);
	std::vector<uint8_t>						binary(m_hasFaultBinary ? size_t(counts.vendorBinarySize) : size_t(0));
	counts.vendorBinarySize = VkDeviceSize(binary.size());
	VkDeviceFaultInfoEXT info{};
	info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
	info.pAddressInfos = addresses.empty() ? nullptr : addresses.data();
	info.pVendorInfos = vendors.empty() ? nullptr : vendors.data();
	info.pVendorBinaryData = binary.empty() ? nullptr : binary.data();
	res = m_pfnGetDeviceFaultInfo(device, &counts, &info);
	if ((res != VK_SUCCESS) and (res != VK_INCOMPLETE)) {
		logHandler.Print("  device fault info: vkGetDeviceFaultInfoEXT failed (%d)\n", int(res));
		return;
	}
	logHandler.Print("  device fault: '%s', %u addresses, %u vendor entries, vendor binary %llu bytes\n", info.description,
					 counts.addressInfoCount, counts.vendorInfoCount, static_cast<unsigned long long>(counts.vendorBinarySize));
	for (uint32_t i = 0; i < counts.addressInfoCount; ++i) {
		const VkDeviceFaultAddressInfoEXT&	address = addresses[i];
		uint64_t							precision = (address.addressPrecision > 0) ? uint64_t(address.addressPrecision) : uint64_t(1);
		uint64_t							lower = uint64_t(address.reportedAddress) & ~(precision - 1);
		logHandler.Print("    address %u: %s 0x%llx (range 0x%llx..0x%llx)\n", i, AddressTypeText(address.addressType),
						 static_cast<unsigned long long>(address.reportedAddress), static_cast<unsigned long long>(lower),
						 static_cast<unsigned long long>(lower + precision - 1));
	}
	for (uint32_t i = 0; i < counts.vendorInfoCount; ++i) {
		const VkDeviceFaultVendorInfoEXT& vendor = vendors[i];
		logHandler.Print("    vendor %u: '%s', code 0x%llx, data 0x%llx\n", i, vendor.description, static_cast<unsigned long long>(vendor.vendorFaultCode),
						 static_cast<unsigned long long>(vendor.vendorFaultData));
	}
	if (not binary.empty())
		WriteVendorBinary(binary.data(), binary.size());
}


void DeviceDiagnostics::WriteVendorBinary(const uint8_t* data, size_t size)
noexcept
{
	char		path[1024];
	const char*	logPath = logHandler.Path();
	const char*	slash = std::strrchr(logPath, '/');
	const char*	backslash = std::strrchr(logPath, '\\');
	if ((slash == nullptr) or ((backslash != nullptr) and (backslash > slash)))
		slash = backslash;
	int directoryLength = (slash != nullptr) ? int(slash - logPath + 1) : 0;
	snprintf(path, sizeof(path), "%.*sdevicefault.bin", directoryLength, logPath);
	std::FILE* file = std::fopen(path, "wb");
	if (file == nullptr) {
		logHandler.Print("    vendor binary: cannot write '%s'\n", path);
		return;
	}
	size_t written = std::fwrite(data, 1, size, file);
	std::fclose(file);
	logHandler.Print("    vendor binary: %llu of %llu bytes written to '%s'\n", static_cast<unsigned long long>(written),
					 static_cast<unsigned long long>(size), path);
}


void DeviceDiagnostics::ReportCheckpoints(void)
noexcept
{
	if (not m_hasCheckpoints) {
		logHandler.Print("  checkpoints: not available on this device\n");
		return;
	}
	VkQueue		queue = vkContext.GraphicsQueue();
	uint32_t	count = 0;
	m_pfnGetQueueCheckpointData(queue, &count, nullptr);
	VkCheckpointDataNV					blank{};
	blank.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
	std::vector<VkCheckpointDataNV>	data(count, blank);
	if (count > 0)
		m_pfnGetQueueCheckpointData(queue, &count, data.data());
	logHandler.Print("  checkpoints: %u (last mark the GPU reached per pipeline stage)\n", count);
	for (uint32_t i = 0; i < count; ++i) {
		char		text[kTextSize];
		uint32_t	serial = uint32_t(reinterpret_cast<uintptr_t>(data[i].pCheckpointMarker));
		const char*	stage = StageText(data[i].stage);
		NoteGpuMark(serial);
		FormatMark(serial, text, sizeof(text));
		if (stage != nullptr)
			logHandler.Print("    %s: %s\n", stage, text);
		else
			logHandler.Print("    stage 0x%x: %s\n", unsigned(data[i].stage), text);
	}
}


void DeviceDiagnostics::ReportBufferMarkers(void)
noexcept
{
	if (m_markerData == nullptr) {
		logHandler.Print("  buffer markers: not available on this device\n");
		return;
	}
	char started[kTextSize];
	char finished[kTextSize];
	NoteGpuMark(m_markerData[0]);
	NoteGpuMark(m_markerData[1]);
	FormatMark(m_markerData[0], started, sizeof(started));
	FormatMark(m_markerData[1], finished, sizeof(finished));
	logHandler.Print("  buffer markers: last started %s\n", started);
	logHandler.Print("  buffer markers: last finished %s\n", finished);
}


void DeviceDiagnostics::ReportRecentMarks(void)
noexcept
{
	uint32_t last = m_serial.load();
	uint32_t count = (last < kRecentMarks) ? last : kRecentMarks;
	logHandler.Print("  last %u marks recorded on the CPU:\n", count);
	for (uint32_t serial = last - count + 1; (count > 0) and (serial <= last); ++serial) {
		char text[kTextSize];
		FormatMark(serial, text, sizeof(text));
		logHandler.Print("    %s\n", text);
	}
}

// =================================================================================================
