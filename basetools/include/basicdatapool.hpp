// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <new>


// =================================================================================================

template <typename ITEM_T>
class BasicDataPool {
protected:
	ITEM_T*			m_itemPool;
	int*			m_freeItems;
	int				m_capacity;
	int				m_freeItemCount;
	bool			m_isCreated;

public:
	BasicDataPool()
		: m_itemPool(nullptr), m_freeItems(nullptr), m_capacity(0), m_freeItemCount(0), m_isCreated(false)
	{
	}


	~BasicDataPool() {
		Destroy();
	}


	inline bool Create(int capacity, bool createOnce = true) {
		return m_isCreated = Setup(capacity, createOnce);
	}


	bool Setup(int capacity, bool createOnce) {
		if (capacity <= 0)
			return false;
		if (createOnce and m_isCreated)
			return true;

		Destroy();

		m_itemPool = reinterpret_cast<ITEM_T*>(malloc(capacity * sizeof(ITEM_T))); // new DataItem<ITEM_T>[capacity];
		m_freeItems = reinterpret_cast<int*>(malloc(capacity * sizeof(*m_freeItems))); // new int[capacity];

		if (not (m_itemPool and m_freeItems)) {
			Destroy();
			return false;
		}

		if constexpr (std::is_trivially_destructible<ITEM_T>::value) {
			memset(m_itemPool, 0, capacity * sizeof(ITEM_T));
		}
		else {
			for (int i = 0; i < m_capacity; i++)
				new(m_itemPool + i) ITEM_T();
		}
		for (int i = 0; i < capacity; i++)
			m_freeItems[i] = capacity - i - 1;

		m_capacity =
		m_freeItemCount = capacity;
		return true;
	}


	void Destroy(void) {
		m_freeItemCount = 0;
		if (m_itemPool) {
			if constexpr (not std::is_trivially_destructible<ITEM_T>::value) {
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
		m_isCreated = false;
	}


	ITEM_T* Claim(int& itemIndex) {
		if (not m_freeItemCount)
			return nullptr;
		itemIndex = m_freeItems[--m_freeItemCount];
#ifdef _DEBUG
		m_freeItems[m_freeItemCount] = -1;
#endif
		ITEM_T* item = m_itemPool + itemIndex;
		if constexpr (not std::is_trivially_constructible<ITEM_T>::value) {
			new(item) ITEM_T();
		}
		//m_freeItems[m_freeItemCount] = -1;
		return item;
	}


	inline ITEM_T* Release(int itemIndex) {
		m_freeItems[m_freeItemCount++] = itemIndex;
		return m_itemPool + itemIndex;
	}


	ITEM_T& operator[](int i) {
		return this->m_itemPool[i];
	}


	inline int Capacity(void) {
		return m_capacity;
	}


	inline int* GetFreeItems(void) {
		return m_freeItems;
	}


	inline int FreeItemCount(void) {
		return m_freeItemCount;
	}

	inline int ItemIndex(ITEM_T* item) {
		return item - m_itemPool;
	}

	inline ITEM_T* GetDataPool() {
		return m_itemPool;
	}

};

// =================================================================================================
