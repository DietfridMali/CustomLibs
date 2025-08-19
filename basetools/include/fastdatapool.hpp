// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <new>

#include "basicdatapool.hpp"
#include "avltree.hpp"

// =================================================================================================

template <typename KEY_T, typename ITEM_T>
class DataPool : public BasicDataPool<ITEM_T> {

	using Comparator = typename AVLTreeTraits<KEY_T, int>::Comparator;

	using ItemProcessor = bool(*)(const ITEM_T&);

private:
	AVLTree<KEY_T, int>*	m_usedItems;
	ItemProcessor			m_itemProcessor;

public:
	FastDataPool()
		: m_itemPool(nullptr), m_freeItems(nullptr), m_usedItems(nullptr), m_freeItemCount(0), m_isCreated(false)
	{
	}


	~FastDataPool() {
		Destroy();
	}


	bool Setup(int capacity, bool createOnce, Comparator comparator) {
		if (createOnce and m_isCreated)
			return true;
		if (!this->DataPool::Setup(capacity, createOnce))
			return false;
		if (!(m_usedItems = new AVLTree<KEY_T, int>(capacity, comparator))) {
			

	}


	inline bool Create(int capacity, bool createOnce = true, Comparator comparator = nullptr) {
		return m_isCreated = Setup(capacity, createOnce);
	}


	void Destroy(void) {
		delete m_usedItems;
		m_usedItems = nullptr;
		this->DataPool::Destroy();
	}


	ITEM_T* FindItem(KEY_T key) {
		if (!m_usedItems)
			return nullptr;
		int i;
		if (!m_usedItems->Extract(key, i))
			return nullptr;
		return m_itemPool + i;
	}



	ITEM_T* Claim(const KEY_T& key) {
		if (!m_usedItems)
			return nullptr;
		if (!m_freeItemCount)
			return nullptr;
		ITEM_T* item = Claim(&i);
		if (!item)
			return nullptr;
		m_usedItems.Insert(key, i);
		return item;
	}


	void Release(const KEY_T& key) {
		if (m_usedItems) {
			int i;
			if (m_usedItems->Extract(key, i))
				this->DataPool::Release(i);
		}
	}


	bool AvlNodeProcessor(const int& i) {
		m_itemProcessor(m_itemPool[i]);
	}


	bool AvlNodeProcessor(const int& i) {
		m_itemProcessor(m_itemPool[i]);
	}


	bool WalkItems(ItemProcessor processor) {
		m_itemProcessor = processor;
		return m_usedItems ? m_usedItems->Walk(AvlNodeProcessor) : false;
	}
	};


// =================================================================================================
