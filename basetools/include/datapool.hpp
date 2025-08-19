// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <new>
#include <cstdlib>

#include "basicdatapool.hpp"
#include "avltree.hpp"

// =================================================================================================

template <typename KEY_T, typename ITEM_T>
class DataPool : public BasicDataPool<ITEM_T> {

	using Comparator = typename AVLTreeTraits<KEY_T, int>::Comparator;

	using DataProcessor = typename AVLTreeTraits<KEY_T, int>::DataProcessor;

	using ItemProcessor = bool(*) (void* t, ITEM_T&);

	using ItemMap = AVLTree<KEY_T, int>;

private:
	ItemMap*	m_usedItems;
	KEY_T		m_itemKey;


public:
	DataPool()
		: BasicDataPool<ITEM_T>(), m_usedItems(nullptr), m_itemKey(0) //nullptr)
	{
	}


	~DataPool() {
		Destroy();
	}


private:
	bool Setup(int32_t capacity, Comparator comparator, void* context, bool createOnce) {
		if (createOnce and this->m_isCreated)
			return true;
		if (not this->BasicDataPool<ITEM_T>::Setup(capacity, createOnce))
			return false;
		void* buffer = malloc(sizeof(ItemMap));
		if (not buffer) {
			Destroy();
			return false;
		}
		m_usedItems = new(buffer) ItemMap(capacity);
		m_usedItems->SetComparator(comparator, context);
		return true;
	}


public:
	inline bool Create(int32_t capacity, Comparator comparator, void* context = nullptr, bool createOnce = true) {
		return this->m_isCreated = Setup(capacity, comparator, context, createOnce);
	}


	void Destroy(void) {
		if (m_usedItems) {
			//delete m_usedItems;
			m_usedItems->~ItemMap();
			free(m_usedItems);
			m_usedItems = nullptr;
		}
		this->BasicDataPool<ITEM_T>::Destroy();
	}


	ITEM_T* FindItem(const KEY_T& key) {
		if (not m_usedItems)
			return nullptr;
		int* itemIndex = m_usedItems->Find(key);
		if (not itemIndex)
			return nullptr;
		return this->m_itemPool + *itemIndex;
	}


	ITEM_T* Claim(const KEY_T& key) {
//		if (not key)
//			return nullptr;
		if (not m_usedItems)
			return nullptr;
		int itemIndex;
		ITEM_T* item = this->BasicDataPool<ITEM_T>::Claim(itemIndex);
		if (not item)
			return nullptr;
		KEY_T nullKey = (KEY_T)0;
		m_usedItems->Insert2(key, itemIndex, nullKey, true);
		int* pi = m_usedItems->Find(key);
		if (not pi) {
			fprintf(stderr, "AVL tree error (%lld not found)\n", ptrdiff_t(key));
			m_usedItems->Find(key);
		}
		fprintf(stderr, "claiming memory block #%d\n", itemIndex);
		return item;
	}


	ITEM_T* Release(const KEY_T& key) {
		if (not m_usedItems)
			return nullptr;
		int itemIndex = -1;
		{
			int* dataNode = m_usedItems->Find(key);
			if (not dataNode)
				fprintf(stderr, "                                                item index #%d not found\n", itemIndex);
		}
		if (not m_usedItems->Extract(key, itemIndex)) {
			char* address = reinterpret_cast<char*>(key) + 11;
			ITEM_T* itemPool = this->BasicDataPool<ITEM_T>::GetDataPool();
			int* freeItems = this->BasicDataPool<ITEM_T>::GetFreeItems();
			for (int i = this->BasicDataPool<ITEM_T>::FreeItemCount(), j = this->BasicDataPool<ITEM_T>::Capacity(); i < j; i++) {
				int h = freeItems[i];
				if (itemPool[h].address == address) {
					itemIndex = h;
					break;
				}
			}
			if (itemIndex < 0)
				return nullptr;
		}
		else {
			typename Map<KEY_T, int>::AVLNode* dataNode = m_usedItems->FindData(itemIndex);
			if (dataNode)
				fprintf(stderr, "                                                duplicate item index #%d\n", itemIndex);
		}
		fprintf(stderr, "                                                releasing memory block #%d\n", itemIndex);
		return this->BasicDataPool<ITEM_T>::Release(itemIndex);
	}


	AVLTree<KEY_T, int>& UsedItems(void) {
		return *m_usedItems;
	}
};

// =================================================================================================
