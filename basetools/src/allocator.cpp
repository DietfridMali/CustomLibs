#define NOMINMAX

#include <cstdlib>
#include <new>
#include "allocator.h"

static bool initializing = false;

static MemoryDescriptor* itemPool = nullptr;

inline void InitAllocator(int capacity = 1000000) {
	if (not initializing) {
		initializing = true;
		static bool initialized = (MemoryManager::Instance().Create(capacity, true), true);
		itemPool = MemoryManager::Instance().GetDataPool();
		initializing = false;
		(void)initialized; // suppress unused warning if needed
	}
}

// =================================================================================================

#if DEBUG_MALLOC

void* Allocator::operator new(std::size_t size) {
	::InitAllocator();
	return MemoryManager::Instance().Alloc(size);
}

void Allocator::operator delete(void* ptr) noexcept {
	MemoryManager::Instance().Free(ptr);
}

void* Allocator::operator new[](std::size_t size) {
	::InitAllocator();
	return MemoryManager::Instance().Alloc(size);
}

void Allocator::operator delete[](void* ptr) noexcept {
	MemoryManager::Instance().Free(ptr);
}

#endif

// =================================================================================================

#if DEBUG_MALLOC

void* operator new(std::size_t size) {
	InitAllocator();
	return MemoryManager::Instance().Alloc(size);
}

void operator delete(void* ptr) noexcept {
	MemoryManager::Instance().Free(ptr);
}

void* operator new[](std::size_t size) {
	InitAllocator();
	return MemoryManager::Instance().Alloc(size);
}

void operator delete[](void* ptr) noexcept {
	MemoryManager::Instance().Free(ptr);
}

#endif

// =================================================================================================
