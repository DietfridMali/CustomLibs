// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <new>

template <typename KEY_T, typename DATA_T>
class AVLTree;

#include "avltreetraits.h"

// =================================================================================================
// list of data buffers for other data containers
// Provide a pool of data items of type _T to avoid having to frequently 
// call the OS's memory allocator to allocate each data item.
// Use DataPool if you know you will have to allocate a huge number of data items.

template <typename ITEM_T>
class DataItem {
public:
	int		m_usedListIndex; // index in used list
	ITEM_T	m_data;
	int		m_pred;
	int		m_succ;

	ITEM_T& operator& () {
		return m_data;
	}

	inline int UsedListIndex() {
		return m_usedListIndex;
	}

	inline ITEM_T& Data() {
		return m_data;
	}
#if 0
	auto operator*() {
		if constexpr (std::is_pointer_v<ITEM_T>) {
			return *m_data;
		}
		else {
			return m_data;
		}
	}
#endif
};

// =================================================================================================

template <typename KEY_T, typename ITEM_T>
class DataPool {

	using Comparator = typename AVLTreeTraits<KEY_T, int>::Comparator;

	using ItemProcessor = bool(*)(const ITEM_T&);

private:
		ITEM_T*					m_itemPool;
		int*					m_freeItems;
		int						m_freeItemCount;
		bool					m_isCreated;
		AVLTree<KEY_T, int>*	m_usedItems;
		ItemProcessor			m_itemProcessor;

	public:
		DataPool() 
			: m_itemPool(nullptr), m_freeItems(nullptr), m_usedItems(nullptr), m_freeItemCount(0), m_isCreated(false)
		{ }


		~DataPool() { 
			Destroy (); 
		}


		bool Setup(int capacity, bool createOnce, Comparator comparator = nullptr);


		inline bool Create(int capacity, bool createOnce = true, Comparator comparator = nullptr) {
			return m_isCreated = Setup (capacity, createOnce);
		}


		void Destroy(void);


		ITEM_T* FindItem(KEY_T key);


		ITEM_T* Claim(const KEY_T& key);


		inline void ReleaseByIndex(int i) {
			m_freeItems[m_freeItemCount++] = i;
			}


		inline void Release(ITEM_T* item) {
			m_freeItems[m_freeItemCount++] = int (item - m_itemPool);
		}


		bool AvlNodeProcessor(const int& i) {
			m_itemProcessor(m_itemPool[i]);
		}


		bool WalkItems(ItemProcessor processor);


		void Release(const KEY_T& key);
	};


	// =================================================================================================
