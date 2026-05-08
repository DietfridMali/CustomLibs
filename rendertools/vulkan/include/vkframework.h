#pragma once

// WIN32_LEAN_AND_MEAN is intentionally NOT set: shader_compiler.cpp pulls in <dxc/dxcapi.h>,
// which references BSTR / IStream / IUnknown from the OLE/COM portion of <windows.h>. With
// lean-and-mean those typedefs are stripped and DXC stops compiling.
#define NOMINMAX
#define NOGDI       // suppress GDI declarations (Rectangle, Ellipse, ...) — not needed
#include <windows.h>

// Vulkan core (VK 1.3 minimum: dynamic_rendering + synchronization2 are core)
#include <vulkan/vulkan.h>

// Vulkan Memory Allocator (single-header, AMD GPUOpen)
#include "vk_mem_alloc.h"

// Link the Vulkan loader (direct-link against vulkan-1.lib from the LunarG SDK)
#pragma comment(lib, "vulkan-1.lib")
