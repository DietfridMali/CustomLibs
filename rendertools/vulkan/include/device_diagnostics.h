#pragma once

#include "vkframework.h"
#include "basesingleton.hpp"

#include <atomic>
#include <cstdint>

// =================================================================================================

class DeviceDiagnostics
	: public BaseSingleton<DeviceDiagnostics> {
public:
	enum class MarkKind : uint8_t {
		ListBegin,
		ListEnd,
		Stage,
		Pipeline,
		Compute,
		OneShotBegin,
		OneShotEnd,
		Draw
	};

	static constexpr uint32_t	kRingSize = 32768;
	static constexpr size_t		kNameSize = 96;
	static constexpr size_t		kTextSize = 256;
	static constexpr uint32_t	kRecentMarks = 32;
	static constexpr uint32_t	kWindowContext = 8;
	static constexpr uint32_t	kWindowMax = 256;
	static constexpr uint32_t	kFrameSlots = 2;
	static constexpr uint32_t	kStatQueries = 32;
	static constexpr uint32_t	kStatValues = 6;
	static constexpr uint32_t	kLastStats = 16;

	struct Record {
		uint32_t	serial{ 0 };
		MarkKind	kind{ MarkKind::ListBegin };
		uint64_t	frame{ 0 };
		char		name[kNameSize]{};
	};

	struct DrawStats {
		uint64_t	frame{ 0 };
		uint64_t	values[kStatValues]{};
		double		gpuMs{ -1.0 };
		char		label[kNameSize]{};
	};

	void Select(VkPhysicalDevice physicalDevice, bool hasFaultExtension, bool hasCheckpointExtension, bool hasBufferMarkerExtension)
	noexcept;

	bool Load(VkDevice device)
	noexcept;

	bool Create(VmaAllocator allocator)
	noexcept;

	void Destroy(VmaAllocator allocator)
	noexcept;

	void Mark(VkCommandBuffer cb, MarkKind kind, const char* name)
	noexcept;

	void Report(void)
	noexcept;

	void SetDrawStatistics(bool enable)
	noexcept;

	int32_t BeginDrawStatistics(VkCommandBuffer cb, const char* label)
	noexcept;

	void EndDrawStatistics(VkCommandBuffer cb, int32_t handle)
	noexcept;

	void ResolveDrawStatistics(uint32_t frameIndex)
	noexcept;

	inline bool HasPipelineStatistics(void) const
	noexcept
	{
		return m_hasStatistics;
	}

	inline bool HasHostQueryReset(void) const
	noexcept
	{
		return m_hasHostQueryReset;
	}

	inline bool HasFault(void) const
	noexcept
	{
		return m_hasFault;
	}

	inline bool HasCheckpoints(void) const
	noexcept
	{
		return m_hasCheckpoints;
	}

	inline bool HasBufferMarker(void) const
	noexcept
	{
		return m_hasBufferMarker;
	}

	inline VkPhysicalDeviceFaultFeaturesEXT& FaultFeatures(void)
	noexcept
	{
		return m_faultFeatures;
	}

private:
	VkPhysicalDeviceFaultFeaturesEXT	m_faultFeatures{};
	bool								m_hasFault{ false };
	bool								m_hasFaultBinary{ false };
	bool								m_hasCheckpoints{ false };
	bool								m_hasBufferMarker{ false };
	PFN_vkGetDeviceFaultInfoEXT			m_pfnGetDeviceFaultInfo{ nullptr };
	PFN_vkCmdSetCheckpointNV			m_pfnCmdSetCheckpoint{ nullptr };
	PFN_vkGetQueueCheckpointDataNV		m_pfnGetQueueCheckpointData{ nullptr };
	PFN_vkCmdWriteBufferMarkerAMD		m_pfnCmdWriteBufferMarker{ nullptr };
	VkBuffer							m_markerBuffer{ VK_NULL_HANDLE };
	VmaAllocation						m_markerAllocation{ VK_NULL_HANDLE };
	volatile uint32_t*					m_markerData{ nullptr };
	std::atomic<uint32_t>				m_serial{ 0 };
	std::atomic<bool>					m_isReported{ false };
	uint32_t							m_gpuLow{ 0 };
	uint32_t							m_gpuHigh{ 0 };
	bool								m_hasStatistics{ false };
	bool								m_hasHostQueryReset{ false };
	bool								m_hasTimestamps{ false };
	bool								m_statsEnabled{ false };
	float								m_timestampPeriod{ 0.0f };
	uint64_t							m_timestampMask{ 0 };
	VkQueryPool							m_statsPool{ VK_NULL_HANDLE };
	VkQueryPool							m_timePool{ VK_NULL_HANDLE };
	uint32_t							m_statCount[kFrameSlots]{};
	uint64_t							m_statFrame[kFrameSlots]{};
	bool								m_statEnded[kFrameSlots][kStatQueries]{};
	char								m_statLabels[kFrameSlots][kStatQueries][kNameSize]{};
	DrawStats							m_lastStats[kLastStats];
	uint32_t							m_lastStatCount{ 0 };
	Record								m_records[kRingSize];

	void FormatMark(uint32_t serial, char* text, size_t size) const
	noexcept;

	void CreateStatisticsPools(void)
	noexcept;

	void LogDrawStats(const char* prefix, const DrawStats& stats) const
	noexcept;

	void ReportLastStats(void)
	noexcept;

	void NoteGpuMark(uint32_t serial)
	noexcept;

	void ReportGpuWindow(void)
	noexcept;

	void ReportFault(void)
	noexcept;

	void ReportCheckpoints(void)
	noexcept;

	void ReportBufferMarkers(void)
	noexcept;

	void ReportRecentMarks(void)
	noexcept;

	void WriteVendorBinary(const uint8_t* data, size_t size)
	noexcept;
};

#define deviceDiagnostics DeviceDiagnostics::Instance()

// =================================================================================================
