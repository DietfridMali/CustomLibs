#pragma once

#if !defined(USE_TRACY)
#  define USE_TRACY 0
#endif

#if USE_TRACY
#  ifndef TRACY_ENABLE
#		define TRACY_ENABLE
#	endif

#	include <tracy/Tracy.hpp>
#	include <cstdint>                 

#	if defined(OPENGL)
#		include <glew.h>
#		include <tracy/TracyOpenGL.hpp>   // GL GPU profiling; glew.h above provides the GL symbols it needs
#	elif defined(DIRECTX)
#		ifndef NOMINMAX
#			define NOMINMAX
#		endif
#		include <tracy/TracyD3D12.hpp>   // DX12 GPU profiling
#	else
#		include <vulkan/vulkan.h>
#		include <tracy/TracyVulkan.hpp>   // Vulkan GPU profiling
#	endif

#else

#	define ZoneScoped
#	define ZoneScopedN(x)
#	define ZoneScopedC(c)
#	define ZoneScopedNC(x, c)
#	define FrameMark
#	define FrameMarkNamed(x)
#	define FrameMarkStart(x)
#	define FrameMarkEnd(x)
#	define TracyMessage(text, size)
#	define TracyMessageL(text)
#	define TracyPlot(name, value)
#	define TracyPlotConfig(name, type, step, fill, color)
#	define TracyGpuContext
#	define TracyGpuZone(x)
#	define TracyGpuCollect

#	ifdef DIRECTX
		using TracyD3D12Ctx = void*;
		namespace tracy { class D3D12ZoneScope; }          // Pointer-Member m_gpuZone — Forward-Decl genügt

#		define TracyD3D12Context(device, queue) nullptr   // -> m_gpuProfilerCtx = nullptr
#		define TracyD3D12Destroy(ctx)
#		define TracyD3D12NewFrame(ctx)
#		define TracyD3D12Collect(ctx)
#	endif

#endif
