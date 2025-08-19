#pragma once

#include "std_defines.h"

#include <algorithm>

#include "type_helper.hpp"
#include "datapool.hpp"

// =================================================================================================

class MemoryDescriptor {
public:
	using Address = char*;

	Address	address;
	int32_t	size;
	bool	isManaged;

	MemoryDescriptor()
		: address(nullptr), size(0), isManaged(true)
	{ }
};

// =================================================================================================

class MemoryManager {
public:
	using Address = char*;
	using Key = ptrdiff_t;

	//DataPool<Address, MemoryDescriptor>	m_memoryDescriptors;
	Address		memoryPool = nullptr;
	Address		memoryStart = nullptr, memoryEnd = nullptr;
	bool		allocFromStart = false;
	Key			m_key;

	DataPool<Key, MemoryDescriptor>	m_memoryDescriptors;

public:
#if 1
	MemoryManager()
		: m_memoryDescriptors()
	{ 
		InitializeAnyType(m_key);
	}

	~MemoryManager() {
		Destroy();
	}
#endif

	Address Reserve(uint32_t size);

	static int KeyComparer(void* context, const Key& searchKey, const Key& dataKey) {
		return (searchKey < dataKey) ? -1 : (searchKey > dataKey) ? 1 : 0;
	}

	bool Create(int capacity, bool createOnce = true);

	void Destroy(void);

	bool IsIntact(int itemIndex);

	bool IsIntact(MemoryDescriptor& md);

	bool IsAddress(int itemIndex);

	static bool ItemProcessor(void* context, const Key& key, const int& itemIndex);
		
	static bool ItemFinder(void* context, const Key& key, const int& itemIndex);

	bool CheckIntegrity(void);

	MemoryDescriptor* Claim(uint32_t size);

	void* Alloc(uint32_t size);

	void* Realloc(void* address, uint32_t size, bool bCopy);

	void Free(void* address);

	void* SetPtr(void* address, uint32_t size);

	inline Key ToKey(void* address) {
		return Key(ptrdiff_t(Address(address) - memoryPool) & 0xFFFFFFF);
	}

	inline MemoryDescriptor* GetDataPool() {
		return m_memoryDescriptors.GetDataPool();
	}

	static MemoryManager& Instance() {
		static MemoryManager instance;
		return instance;
	}

private:
	inline MemoryDescriptor* Find(Key key) {
		return m_memoryDescriptors.FindItem(key);
	}

	//MemoryManager() = default;
	//~MemoryManager() = default;

	MemoryManager(const MemoryManager&) = delete;
	MemoryManager& operator=(const MemoryManager&) = delete;
};

// =================================================================================================
