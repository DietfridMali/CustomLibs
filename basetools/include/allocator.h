#pragma once

#include <algorithm>
#include "std_defines.h"

#include "memorymanager.h"

#define DEBUG_MALLOC 0

// =================================================================================================

class Allocator {
public:
#if DEBUG_MALLOC
	static void* operator new(std::size_t size);

	static void operator delete(void* ptr) noexcept;

	static void* operator new[](std::size_t size);

	static void operator delete[](void* ptr) noexcept;
#endif
};

// =================================================================================================
