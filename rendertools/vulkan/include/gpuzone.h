#pragma once

#include "tracy_wrapper.h"
#include "commandlist.h"

// A GPU profiling zone inside the current command list, spelled the same in every backend.
// OpenGL's TracyGpuZone takes the name alone; DX12 and Vulkan need the Tracy context and the
// command list the timestamps are written on, which only rendertools knows - hence this header.
//
// Not Tracy's own VkCtxScope: that one keeps the command buffer it was started on and writes the end
// timestamp into the same one. A zone that spans a render target switch outlives its command list
// (DrawBufferHandler::ActivateDrawBuffer () closes the list of the target it parks), the end timestamp
// went into a command buffer that was no longer recording, its query never became available and
// VkCtx::Collect () stopped advancing for good - until the query pool had wrapped around and its
// assert fired. Tracy only needs the two timestamps to run in submission order, so the end goes into
// whatever list is recording when the zone is left.

#if USE_TRACY

class GfxGpuZoneScope
{
public:
    inline GfxGpuZoneScope(const tracy::SourceLocationData* srcloc) noexcept
    {
        uint16_t queryId = 0;

#ifdef TRACY_ON_DEMAND
        if (not tracy::GetProfiler().IsConnected())
            return;
#endif
        m_active = WriteTimestamp(queryId);
        if (not m_active)
            return;

        auto item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuZoneBeginSerial);
        tracy::MemWrite(&item->gpuZoneBegin.cpuTime, tracy::Profiler::GetTime());
        tracy::MemWrite(&item->gpuZoneBegin.srcloc, uint64_t(srcloc));
        tracy::MemWrite(&item->gpuZoneBegin.thread, tracy::GetThreadHandle());
        tracy::MemWrite(&item->gpuZoneBegin.queryId, queryId);
        tracy::MemWrite(&item->gpuZoneBegin.context, commandListHandler.m_gpuProfilerCtx->GetId());
        tracy::Profiler::QueueSerialFinish();
    }

    inline ~GfxGpuZoneScope() noexcept
    {
        if (not m_active)
            return;

        uint16_t queryId = 0;

        if (not WriteTimestamp(queryId))
            return;

        auto item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuZoneEndSerial);
        tracy::MemWrite(&item->gpuZoneEnd.cpuTime, tracy::Profiler::GetTime());
        tracy::MemWrite(&item->gpuZoneEnd.thread, tracy::GetThreadHandle());
        tracy::MemWrite(&item->gpuZoneEnd.queryId, queryId);
        tracy::MemWrite(&item->gpuZoneEnd.context, commandListHandler.m_gpuProfilerCtx->GetId());
        tracy::Profiler::QueueSerialFinish();
    }

    GfxGpuZoneScope(const GfxGpuZoneScope&) = delete;
    GfxGpuZoneScope& operator=(const GfxGpuZoneScope&) = delete;

private:
    bool    m_active { false };

    static inline bool WriteTimestamp(uint16_t& queryId) noexcept
    {
        TracyVkCtx ctx = commandListHandler.m_gpuProfilerCtx;
        if (not ctx)
            return false;
        VkCommandBuffer cb = commandListHandler.CurrentGfxList();
        if (cb == VK_NULL_HANDLE)
            cb = commandListHandler.UploadCmdBuffer();
        if (cb == VK_NULL_HANDLE)
            return false;
        queryId = uint16_t(ctx->NextQueryId());
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ctx->GetQueryPool(), queryId);
        return true;
    }
};

#define GfxGpuZone(name) \
    static constexpr tracy::SourceLocationData TracyConcat(__gfx_gpu_source_location, TracyLine) { name, TracyFunction, TracyFile, uint32_t(TracyLine), 0 }; \
    GfxGpuZoneScope TracyConcat(__gfx_gpu_zone, TracyLine)(&TracyConcat(__gfx_gpu_source_location, TracyLine))

#else

#define GfxGpuZone(name)

#endif
