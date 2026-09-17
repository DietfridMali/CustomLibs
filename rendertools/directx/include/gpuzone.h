#pragma once

#include "tracy_wrapper.h"
#include "commandlist.h"

// A GPU profiling zone inside the current command list, spelled the same in every backend.
// OpenGL's TracyGpuZone takes the name alone; DX12 and Vulkan need the Tracy context and the
// command list the timestamps are written on, which only rendertools knows - hence this header.

#define GfxGpuZone(name)	TracyD3D12Zone(commandListHandler.m_gpuProfilerCtx, commandListHandler.CurrentGfxList(), name)
