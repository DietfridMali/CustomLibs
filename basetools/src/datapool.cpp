// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <new>

#include "datapool.hpp"

template <typename KEY_T, typename ITEM_T>
class AvlTree;

// =================================================================================================

bool DataPool<KEY_T, ITEM_T>::Setup(int capacity, bool createOnce, Comparator comparator = nullptr) {
	if (capacity <= 0)
		return false;
	if (createOnce && m_isCreated)
		return true;
	Destroy();
	m_itemPool = reinterpret_cast<DataItem<ITEM_T>*>(malloc(capacity * sizeof(DataItem<ITEM_T>))); // new DataItem<ITEM_T>[capacity];
	if constexpr (!std::is_trivially_constructible<ITEM_T>::value) {
		for (int i = 0; i < capacity; i++)
			new(&m_itemPool[i].m_data) ITEM_T();
	}
	m_freeItems = reinterpret_cast<int*>(malloc(capacity * sizeof(*m_freeItems))); // new int[capacity];

	if (comparator)
		m_usedItems = new AvlTree<KEY_T, int>(capacity, comparator); 

	if (!(m_itemPool && m_freeItems && m_usedItems)) {
		Destroy();
		return false;
	}

	memset(m_itemPool, 0, sizeof(*m_itemPool) * capacity);
	for (int i = 0; i < capacity; i++) {
		m_itemPool[i].m_usedListIndex = -1;
		m_freeItems[i] = capacity - i - 1;
	}

	m_capacity =
	m_freeItemCount = capacity;
	return true;
}


void DataPool<KEY_T, ITEM_T>::Destroy(void) {
	m_freeItemCount = 0;
	if (m_itemPool) {
		if constexpr (!std::is_trivially_destructible<ITEM_T>::value) {
			for (int i = 0; i < m_capacity; i++)
				m_itemPool[i].~ITEM_T();
		}
		free(m_itemPool); // delete[] m_itemPool;
		m_itemPool = nullptr;
	}
	if (m_freeItems) {
		free(m_freeItems); // delete[] m_freeItems;
		m_freeItems = nullptr;
	}
	if (m_usedItems) {
		free(m_usedItems); // delete[] m_usedItems;
		m_usedItems = nullptr;
	}
	m_isCreated = false;
}


ITEM_T* DataPool<KEY_T, ITEM_T>::FindItem(KEY_T& key) {
	if (!m_usedItems)
		return nullptr;
		int i;
	if (!m_usedItems->Extract(key, i))
		return nullptr;
	return m_itemPool + i;
}


ITEM_T* DataPool<KEY_T, ITEM_T>::Claim(KEY_T& key) {
	if (!m_freeItemCount)
		return nullptr;
	//fprintf(stderr, "claiming data pool item #%d\n", i);
	 int itemIndex;
	 ITEM_T* item = this->BasicDataPool::Claim(itemIndex);
	m_usedItems.Insert(key, i);
	m_freeItems[m_freeItemCount] = -1;
	return item;
}


void DataPool<KEY_T, ITEM_T>::Release(const KEY_T& key) {
	if (m_usedItems) {
		int i;
		if (m_usedItems->Extract(key, i)) 
			m_freeItems[m_freeItemCount++] = i;
	}
}

bool DataPool<KEY_T, ITEM_T>::WalkItems(ItemProcessor processor) {
	m_itemProcessor = processor;
	return m_usedItems ? m_usedItems->Walk(AvlNodeProcessor) : false;
}




// =================================================================================================
