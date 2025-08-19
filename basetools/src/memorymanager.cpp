#define NOMINMAX

#include "allocator.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <new>

typedef struct { int data; int key; } avlTestData;

avlTestData avlTestSet[] = {
#if 0
	{ 0, 163371584 },
	{ 1, 163442016 },
	{ 2, 163538944 },
	{ 3, 163377040 },
	{ 4, 163565168 },
	{ 5, 163566064 },
	{ 6, 163567600 },
	{ 7, 163565296 },
	{ 8, 163567984 },
	{ 9, 163566576 },
	{ 10, 163567088 },
	{ 11, 163564656 },
	{ 12, 163567472 },
	{ 13, 163564912 },
	{ 14, 163567216 },
	{ 15, 163568368 },
	{ 16, 163568112 },
	{ 17, 163568240 },
	{ 18, 163565680 },
	{ 19, 55054912 },
	{ 20, 164165120 },
	{ 21, 164169856 },
	{ 22, 164165504 },
	{ 23, 164168960 },
	{ 24, 164171392 },
	{ 25, 164255552 },
	{ 26, 164169984 },
	{ 27, 164171904 },
	{ -27, 164171904 },
	{ 27, 164253056 },
	{ 28, 164165632 },
	{ 29, 164171904 },
	{ -29, 164171904 },
	{ 29, 54986864 },
	{ 30, 164165248 },
	{ 31, 55020864 },
	{ 32, 164180816 },
	{ 33, 164170112 },
	{ -32, 164180816 },
	{ 32, 164222512 },
	{ -33, 164170112 },
	{ 33, 55020960 },
	{ 34, 164083120 },
	{ 35, 164170112 },
	{ 36, 164170240 },
	{ 37, 164170368 },
	{ 38, 164171904 },
	{ -37, 164170368 },
	{ -38, 164171904 },
	{ -34, 164083120 },
	{ -33, 55020960 },
	{ -35, 164170112 },
	{ 36, 164170240 },
	{ 36, 55020960 },
	{ 35, 164180816 },
	{ 33, 164177232 },
	{ 34, 164170112 },
	{ 38, 164177344 },
	{ 37, 164171904 },
	{ -38 , 164177344 },
	{ -37 , 164171904 },
	{ -35 , 164180816 },
	{ -36 , 55020960 },
	{ -33 , 164177232 },
	{ -34 , 164170112 },
	{ 34, 55020960 },
	{ 33, 55106304 },
	{ 36, 164168064 },
	{ 35, 55105920 },
	{ 37, 164165376 },
	{ -35 , 55105920 } 
#endif
#if 1
{ 0, 69820528 },
{ 1, 264514104 },
{ 2, 69820584 },
{ 3, 264514048 },
{ 4, 69820616 },
{ 5, 264513992 },
{ 6, 69820672 },
{ 7, 264513936 },
{ 8, 69820728 },
{ 9, 264513880 },
{ 10, 69820784 },
{ 11, 264513824 },
{ 12, 69820840 },
{ 13, 264513768 },
{ 14, 69820896 },
{ 15, 264513712 },
{ 16, 69820952 },
{ 17, 264513656 },
{ 18, 69821008 },
{ 19, 264513056 },
{ 20, 69821064 },
{ 21, 264513000 },
{ 22, 69821120 },
{ 23, 264512944 },
{ 24, 69821176 },
{ 25, 264512808 },
{ 26, 69821232 },
{ 27, 264512752 },
{ -27, 264512752 },
{ 27, 69821288 },
{ 28, 264512696 },
{ 29, 69821424 },
{ -29, 69821424 },
{ 29, 264512584 },
{ 30, 69821480 },
{ 31, 264512552 },
{ 32, 69821536 },
{ 33, 264512488 },
{ -32, 69821536 },
{ 32, 69821584 },
{ -33, 264512488 }
#endif
};


// =================================================================================================

bool MemoryManager::Create(int capacity, bool createOnce) {
	memoryPool = memoryStart = reinterpret_cast<Address>(malloc(1000 * 1000 * 1000));
	memoryEnd = memoryStart + 1000 * 1000 * 1000;
	return m_memoryDescriptors.Create(capacity, KeyComparer, this, createOnce);
}


void MemoryManager::Destroy(void) {
	m_memoryDescriptors.Destroy();
}


bool MemoryManager::IsIntact(MemoryDescriptor& md) {
	return md.address and not strncmp(md.address - 11, "@#@#", 4) and not strncmp(md.address + md.size, "@#@#", 4);
}


bool MemoryManager::IsIntact(int itemIndex) {
	return (itemIndex < 0) ? true : IsIntact(m_memoryDescriptors[itemIndex]);
}



bool MemoryManager::IsAddress(int itemIndex) {
	if (ToKey(m_memoryDescriptors[itemIndex].address) == m_key)
		return true;
	return false;
}


bool MemoryManager::ItemProcessor(void* context, const Key& key, const int& itemIndex) {
	return context ? static_cast<MemoryManager*>(context)->IsIntact(itemIndex) : false;
}


bool MemoryManager::ItemFinder(void* context, const Key& key, const int& itemIndex) {
	return context ? static_cast<MemoryManager*>(context)->IsAddress(itemIndex) : false;
}


bool MemoryManager::CheckIntegrity(void) {
//	for (int i = 0; (md = m_memoryDescriptors.UsedItem(i)); i++) {
	return m_memoryDescriptors.UsedItems().Walk(ItemProcessor, this);
	///return m_memoryDescriptors.ProcessItems(this->IsIntact);
}


MemoryManager::Address MemoryManager::Reserve(uint32_t size) {
	if (memoryStart + size > memoryEnd)
		return nullptr;
	allocFromStart = true; // !allocFromStart;
	if (allocFromStart) {
		Address p = memoryStart;
		memoryStart += size;
		return p;
	}
	else {
		memoryEnd -= size;
		return memoryEnd;
	}
}


MemoryDescriptor* MemoryManager::Claim(uint32_t size) {
	CheckIntegrity();
	Address address = Reserve(size + 16); // static_cast<Address>(malloc(size + 16));
	if (not address) {
		//fprintf(stderr, "%s (%d): memory allocation failed\n", __FILE__, __LINE__);
		return nullptr;
	}
#define AVL_TEST 0
#if AVL_TEST
	for (int i = 0; i < sizeof(avlTestSet) / sizeof(*avlTestSet); i++) {
		avlTestData& h = avlTestSet[i];
		int data, key = abs(h.key);
		if (h.key < 0) {
			m_memoryDescriptors.UsedItems().Extract(h.key, data);
			if (m_memoryDescriptors.UsedItems().FindData(h.data))
				fprintf(stderr, "                                                duplicate item index #%d\n", h.data);
		}
		else
			m_memoryDescriptors.UsedItems().Insert(h.key, h.data);
	}
#endif
	Key key = ToKey(address);
	MemoryDescriptor* md = m_memoryDescriptors.Claim(key); // address);
	if (not md) {
		//free(address);
		//fprintf(stderr, "MM::Alloc: out of memory blocks\n");
		return nullptr;
	}
	md->address = address;
	md->size = size;
	memcpy(md->address, "@#@#", 4);
	md->address += 4;
	sprintf(md->address, "%06d", int(md - GetDataPool()));
	md->address += 7;
	memset(md->address, ' ', size);
	memcpy(md->address + size, "@#@#", 5);
	md->isManaged = true;
	//fprintf(stderr, "MM::Alloc: %zd bytes\n", size);
	fprintf(stderr, "Allocating memory buffer '%lld'\n", key);
	return md;
}


void* MemoryManager::Alloc(uint32_t size) {
	MemoryDescriptor* md = Claim(size);
	return md ? reinterpret_cast<void*>(md->address) : nullptr;
}


void* MemoryManager::Realloc(void* address, uint32_t size, bool bCopy) {
	if (not address) {
		//fprintf(stderr, "%s (%d): realloc on nullptr\n", __FILE__, __LINE__);
		return nullptr;
	}

	if (not size) {
		//fprintf(stderr, "%s (%d): realloc to size zero\n", __FILE__, __LINE__);
		return nullptr;
	}

	MemoryDescriptor* mbNew = Claim(size);
	if (not mbNew)
		return address;

	address = Address (address) - 11;
	Key key = ToKey(address);
	MemoryDescriptor* mbOld = m_memoryDescriptors.FindItem(key);
	if (not mbOld) {
		fprintf(stderr, "%s (%d): memory block list is corrupted\n", __FILE__, __LINE__);
		mbOld = m_memoryDescriptors.FindItem(key);
		return address;
	}
	if (mbOld->isManaged) {
		fprintf(stderr, "%s (%d): cannot reallocate unmanaged memory\n", __FILE__, __LINE__);
		return address;
	}

	if (bCopy)
		memcpy(mbNew->address, address, std::min(uint32_t(mbNew->size), uint32_t(mbOld->size)));
	//fprintf(stderr, "MM::Realloc: releasing %zd bytes @ %p\n", md->size, md->address);
	try {
		free(address);
	}
	catch (...) {
		fprintf(stderr, "MM: low level memory corruption encountered\n");
	}
	mbOld->address = nullptr;
	mbOld->size = 0;
	m_memoryDescriptors.Release(key);
	return mbNew->address;
}


void MemoryManager::Free(void* address) {
	address = reinterpret_cast<Address>(address) - 11;
	Key key = ToKey(address);
	//Address key = reinterpret_cast<Address>(address) - 11;
	fprintf(stderr, "                                                Freeing memory buffer '%zd'\n", key);
	MemoryDescriptor* md = m_memoryDescriptors.Release(key);
	if (not md) {
		fprintf(stderr, "%s (%d): memory block list is corrupted\n", __FILE__, __LINE__);
		m_key = key;
		m_memoryDescriptors.UsedItems().Walk(ItemFinder, this);
		return;
	}

	if (not IsIntact(*md)) {
		fprintf(stderr, "%s (%d): memory buffer is corrupted\n", __FILE__, __LINE__);
		address = address;
	}
#if 0
	if (md->isManaged) {
		try {
			free(address);
		}
		catch (...) {
			fprintf(stderr, "MM: low level memory corruption encountered\n");
		}
	}
#endif
	md->address = nullptr;
	md->size = 0;
	md->isManaged = true;
	CheckIntegrity();
}


void* MemoryManager::SetPtr(void* address, uint32_t size) {
	Key key = ToKey(address);
	MemoryDescriptor* md = m_memoryDescriptors.Claim(key);
	if (not md) {
		//fprintf(stderr, "MM::SetPtr: out of memory blocks\n");
		return nullptr;
	}
	md->address = static_cast<Address>(address);
	md->size = size;
	md->isManaged = false;
	return address;
}

// =================================================================================================
