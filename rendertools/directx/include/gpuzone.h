#pragma once

#include "tracy_wrapper.h"
#include "commandlist.h"

// A GPU profiling zone inside the current command list, spelled the same in every backend.
// OpenGL's TracyGpuZone takes the name alone; DX12 and Vulkan need the Tracy context and the
// command list the timestamps are written on, which only rendertools knows - hence this header.
//
// Not Tracy's own D3D12ZoneScope: that one keeps the command list it was started on and writes the end
// timestamp into the same one. A zone that spans a render target switch outlives its command list
// (DrawBufferHandler::ActivateDrawBuffer () closes the list of the target it parks), and EndQuery () on
// a closed list is invalid. Tracy only needs the two timestamps to run in submission order, so the end
// goes into whatever list is recording when the zone is left - and the resolve of the pair with it: that
// list closes after the one the zone began on, so it runs after both queries have been written.

#if USE_TRACY

class GfxGpuZoneScope
{
public:
    inline GfxGpuZoneScope(const tracy::SourceLocationData* srcloc) noexcept
    {
#ifdef TRACY_ON_DEMAND
        if (not tracy::GetProfiler().IsConnected())
            return;
#endif
        TracyD3D12Ctx ctx = commandListHandler.m_gpuProfilerCtx;
        if (not ctx)
            return;
        ID3D12GraphicsCommandList* list = commandListHandler.CurrentGfxList();
        if (not list)
            return;
        m_queryId = ctx->NextQueryId();
        list->EndQuery(ctx->GetQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, m_queryId);
        m_active = true;

        auto item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuZoneBeginSerial);
        tracy::MemWrite(&item->gpuZoneBegin.cpuTime, tracy::Profiler::GetTime());
        tracy::MemWrite(&item->gpuZoneBegin.srcloc, uint64_t(srcloc));
        tracy::MemWrite(&item->gpuZoneBegin.thread, tracy::GetThreadHandle());
        tracy::MemWrite(&item->gpuZoneBegin.queryId, uint16_t(m_queryId));
        tracy::MemWrite(&item->gpuZoneBegin.context, ctx->GetId());
        tracy::Profiler::QueueSerialFinish();
    }

    inline ~GfxGpuZoneScope() noexcept
    {
        if (not m_active)
            return;
        TracyD3D12Ctx ctx = commandListHandler.m_gpuProfilerCtx;
        ID3D12GraphicsCommandList* list = commandListHandler.CurrentGfxList();
        if (not (ctx and list))
            return;

        const uint32_t queryId = m_queryId + 1;

        list->EndQuery(ctx->GetQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, queryId);
        list->ResolveQueryData(ctx->GetQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, m_queryId, 2, ctx->GetReadbackBuffer(), m_queryId * sizeof(uint64_t));

        auto item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuZoneEndSerial);
        tracy::MemWrite(&item->gpuZoneEnd.cpuTime, tracy::Profiler::GetTime());
        tracy::MemWrite(&item->gpuZoneEnd.thread, tracy::GetThreadHandle());
        tracy::MemWrite(&item->gpuZoneEnd.queryId, uint16_t(queryId));
        tracy::MemWrite(&item->gpuZoneEnd.context, ctx->GetId());
        tracy::Profiler::QueueSerialFinish();
    }

    GfxGpuZoneScope(const GfxGpuZoneScope&) = delete;
    GfxGpuZoneScope& operator=(const GfxGpuZoneScope&) = delete;

private:
    bool        m_active { false };
    uint32_t    m_queryId { 0 };
};

#define GfxGpuZone(name) \
    static constexpr tracy::SourceLocationData TracyConcat(__gfx_gpu_source_location, TracyLine) { name, TracyFunction, TracyFile, uint32_t(TracyLine), 0 }; \
    GfxGpuZoneScope TracyConcat(__gfx_gpu_zone, TracyLine)(&TracyConcat(__gfx_gpu_source_location, TracyLine))

#else

#define GfxGpuZone(name)

#endif
