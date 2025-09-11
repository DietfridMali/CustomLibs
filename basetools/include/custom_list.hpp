// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <variant>
#include <utility>
#include <functional>
#include <type_traits>

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <type_traits>
#include <cstddef>
#include <utility>
#include <iostream>

#include "type_helper.hpp"
#include "array.hpp"

//-----------------------------------------------------------------------------

template < typename ITEM_T>
class List
{
	template<typename DATA_T>
	struct GetDataType {
		using type = DATA_T;
	};

	template<typename DATA_T>
	struct GetDataType<SharedPointer<DATA_T>> {
		using type = DATA_T;
	};

	template<typename DATA_T>
	struct IsSharedPointer : std::false_type {};

	template<typename DATA_T>
	struct IsSharedPointer<SharedPointer<DATA_T>> : std::true_type {};

	using ItemType = ITEM_T;
	using DataType = typename GetDataType<ITEM_T>::type;
	//using ItemFilter = bool (*)(ITEM_T*);
	using ItemFilter = std::function<bool>(ITEM_T*);

	typedef int(__cdecl* tComparator) (const ItemType*, const ItemType*);

public:
	class ListNode;

	//----------------------------------------

	class ListNodePtr {
	public:
		ListNode* m_nodePtr;

		ListNodePtr() : m_nodePtr(nullptr) {}

		ListNodePtr(const ListNodePtr& other) {
			m_nodePtr = other.m_nodePtr;
		}

		explicit ListNodePtr(ListNode* p) : m_nodePtr(p) {}

		ListNodePtr& operator= (ListNodePtr other) {
			m_nodePtr = other.m_nodePtr;
			return *this;
		}

		ListNodePtr& operator= (ListNode* p) {
			m_nodePtr = p;
			return *this;
		}

		ListNodePtr& operator= (const ListNode* p) const {
			m_nodePtr = p;
			return *this;
		}

		bool operator== (const ListNodePtr* other) {
			return m_nodePtr == other->m_nodePtr;
		}

		bool operator!= (const ListNodePtr* other) const {
			return m_nodePtr != other->m_nodePtr;
		}

		bool operator== (const ListNodePtr& other) const {
			return m_nodePtr == other.m_nodePtr;
		}

		bool operator!= (const ListNodePtr& other) const {
			return m_nodePtr != other.m_nodePtr;
		}

		bool operator== (const ListNode* nodePtr) const {
			return m_nodePtr == nodePtr;
		}

		bool operator!= (const ListNode* nodePtr) const {
			return m_nodePtr != nodePtr;
		}

		ListNodePtr& operator++ () {
			m_nodePtr = m_nodePtr->m_succ;
			return *this;
		}

		ListNodePtr& operator-- () {
			m_nodePtr = m_nodePtr->m_pred;
			return *this;
		}

		ListNodePtr operator++ (int) {
			ListNodePtr p(m_nodePtr);
			m_nodePtr = m_nodePtr->m_succ;
			return p;
		}

		ListNodePtr operator-- (int) {
			ListNodePtr p(m_nodePtr);
			m_nodePtr = m_nodePtr->m_pred;
			return p;
		}

		ListNodePtr operator+ (int n) {
			ListNodePtr p = *this;
			for (; n; n--)
				++p;
			return p;
		}

		ListNodePtr operator- (int n) {
			ListNodePtr p = *this;
			for (; n; n--)
				--p;
			return p;
		}

		ListNode* operator->() const {
			return m_nodePtr;
		}

		operator bool() {
			return (m_nodePtr != nullptr);
		}

		operator ListNode* const() {
			return m_nodePtr;
		}

		inline ListNodePtr& Pred(void) {
			return m_nodePtr->m_pred;
		}

		inline ListNodePtr& Succ(void) {
			return m_nodePtr->m_succ;
		}

		ItemType& DataValue(void) {
			return m_nodePtr->DataValue();
		}

		ItemType& DataValue() const {
			return m_nodePtr->DataValue();
		}

		DataType* DataPointer() {
			return m_nodePtr->DataPointer();
		}

		ItemType& DataItem() {
			return m_nodePtr->DataItem();
		}

	};

	//----------------------------------------

	class ListNode
	{
		public:
			ListNodePtr	m_pred;
			ListNodePtr	m_succ;
			ItemType	m_dataItem;
			bool		m_manageData;

			explicit ListNode() {
				//m_dataItem = ItemType();
				InitializeAnyType(m_dataItem);
				m_manageData = true;
			}

			ListNode(const ItemType& dataValue, bool manageData = false)
				: m_pred(nullptr), m_succ(nullptr), m_dataItem(dataValue), m_manageData(manageData)
			{
}

			ListNode(ItemType&& dataValue, bool manageData = false)
				: m_pred(nullptr), m_succ(nullptr), m_dataItem(dataValue), m_manageData(manageData)
			{
			}

			ListNode(const ListNode& other) {
				m_dataItem = other.m_dataItem;  // normale Kopie
				m_manageData = other.m_manageData;
				m_pred = nullptr;
				m_succ = nullptr;
			}

			inline ListNode* Pred(void) {
				return m_pred;
			}

			inline ListNode* Succ(void) {
				return m_succ;
			}

			ItemType& DataValue(void) {
#if 0
				if constexpr (IsSharedPointer<ITEM_T>::value)
					return *((DataType*)m_dataItem);
				else
#endif
					return m_dataItem;
			}

			ItemType& DataValue() const {
#if 0
				if constexpr (IsSharedPointer<ITEM_T>::value)
					return *((DataType*)m_dataItem);
				else
#endif
					return m_dataItem;
			}

			DataType* DataPointer() {
#if 1
				if constexpr (IsSharedPointer<ITEM_T>::value)
					return (DataType*)m_dataItem;
				else
#endif
					return &m_dataItem;
			}

			const DataType* DataPointer() const {
#if 1
				if constexpr (IsSharedPointer<ITEM_T>::value)
					return (DataType*)m_dataItem;
				else
#endif
					return &m_dataItem;
			}

			ItemType& DataItem() {
				return m_dataItem;
			}

			const ItemType& DataItem() const {
				return m_dataItem;
			}

			inline void Unlink(void) {
				if (m_pred)
					m_pred->m_succ = m_succ;
				if (m_succ)
					m_succ->m_pred = m_pred;
				if (m_pred)
					m_pred = nullptr;
				if (m_succ)
					m_succ = nullptr;
			}

			operator ItemType& () {
				return DataValue();
			}

			~ListNode() {
				Unlink();
				if constexpr (std::is_pointer_v<ItemType>) {
					if (m_manageData) {
						delete m_dataItem;
						m_dataItem = nullptr;
					}
				}
			}
	};

	//----------------------------------------

	class Iterator {

	private:
		ListNodePtr 	m_first;
		ListNodePtr 	m_last;
		ListNodePtr 	m_current;
		int32_t			m_normalIndex;
		int32_t			m_length;

	public:
		explicit Iterator() {}

		Iterator(List<ItemType>& l)
			: m_first(l.First()), m_last(l.Last()), m_normalIndex(-1), m_length(l.Length())
		{
			m_current = m_first;
		}

		Iterator(const List<ItemType>& l)
			: m_first(l.First()), m_last(l.Last()), m_normalIndex(1), m_length(0)
		{
			m_current = m_first;
		}

		operator bool() const { return m_current != nullptr; }
#if 0
		ItemType* operator*() {
			return m_current.DataPointer();
		}

		const ItemType* operator*() const {
			return m_current;
		}
#endif
		//ItemType* operator*() const { return &m_current->m_dataItem; }
		// return the current value and the index of the value in the list
		ItemType& operator*() {
			return m_current.DataValue();
		}

		ItemType* operator->() {
			return m_current.DataPointer();
		}

		inline Iterator& operator++() {
			++m_current;
			++m_index;
			return *this;
		}

		inline Iterator& operator--() {
			--m_current;
			--m_index;
			return *this;
		}

		inline Iterator& operator++(int) {
			m_current++;
			m_index++;
			return *this;
		}

		inline Iterator& operator--(int) {
			m_current--;
			m_index--;
			return *this;
		}

		bool operator==(const Iterator& other) const {
			return m_current == other.m_current;
		}

		bool operator!=(const Iterator& other) const {
			return m_current != other.m_current;
		}

		bool operator==(const ListNodePtr& other) const {
			return m_current == other;
		}

		bool operator!=(const ListNodePtr& other) const {
			return m_current != other;
		}

		Iterator operator+(int n) const {
			Iterator it = *this;
			while ((n-- > 0) and (it != m_last)) {
				++it;
			}
			return it;
		}

		Iterator operator-(int n) const {
			Iterator it = *this;
			while ((n-- > 0) and (it != m_first)) {
				--it;
			}
			return it;
		}

		//Iterator& Start(void) {
		constexpr Iterator& First(void) {
			m_index = 0;
			m_current = m_first;
			return *this;
		}

		Iterator& Last(void) {
			m_current = m_last;
			return *this;
		}

		inline int32_t Index(void) {
			return m_index;
		}
	};

	// ----------------------------------------
	// This list implementation uses two dummy entries as head and tail elements,
	// as this makes many operations on the list much easier.
	// Since the list can always store a ItemType*, head and tail will be initialized 
	// with a ItemType*, which is quite memory efficient.

protected:
	const char* m_name;
	ListNode*	m_head;
	ListNode*	m_tail;
	ListNodePtr	m_headPtr;
	ListNodePtr	m_tailPtr;
	ItemType*	m_none;

	// holds a copy of the median data for sorting; one global variable should work 
	// as it is only used during the sorting part and not during the recursive descent
	ItemType	m_median;
	tComparator	m_compare;

	int32_t		m_length;
	bool		m_result;
	bool		m_isValid;

public:
	inline void Reset(void) {
		m_head = nullptr;
		m_tail = nullptr;
		m_headPtr = nullptr;
		m_tailPtr = nullptr;
		m_length = 0;
		m_isValid = false;
	}


	inline void Init(void) {
		if (not m_isValid) {
			m_isValid = true;
			m_head = new ListNode();
			m_tail = new ListNode();
			if constexpr (std::is_pointer_v<ItemType>)
				m_none = nullptr;
			else
				m_none = new ItemType();
			m_headPtr = m_head;
			m_tailPtr = m_tail;
			m_headPtr.Pred() =
			m_tailPtr.Succ() = nullptr;
			m_headPtr.Succ() = m_tail;
			m_tailPtr.Pred() = m_head;
			m_length = 0;
		}
	}

	void Clear(void) {
		if (m_head) {
			for (ListNodePtr n = m_headPtr + 1; n != m_tailPtr; ) {
				ListNodePtr p = n;
				++n;
				if (p.m_nodePtr) {
					delete p.m_nodePtr;
					p.m_nodePtr = nullptr;
				}
			}
		}
	}

	void Destroy(void) {
		if (m_isValid) {
			m_isValid = false;
			Clear();
			delete m_head;
			delete m_tail;
			delete m_none;
			m_head = 
			m_tail = nullptr;
			m_none = nullptr;
		}
	}

	List<ItemType>& Copy(const List<ItemType>& other) {
		if (other.Length()) {
			for (ListNode* pn = other.First(); pn != other.GetTail(); pn = pn->Succ())
				AddNode(-1, new ListNode(*pn));
		}
		return *this;
	}

	Iterator begin() const {
		return Iterator(*this).First();
	}

	Iterator end() const {
		return Iterator(*this).Last();
	}

	Iterator rbegin() const {
		return Iterator(*this).ReverseFirst();
	}

	Iterator rend() const {
		return Iterator(*this).ReverseLast();
	}

	inline ListNode* GetHead(void) const {
		return m_head;
	}

	inline ListNode* GetTail(void) const {
		return m_tail;
	}

	inline ListNode* First(void) const {
		return m_length ? m_head->m_succ : m_head;
	}

	inline ListNode* Last(void) const {
		return m_length ? m_tail : m_head;
	}

	inline ListNode* ReverseFirst(void) const {
		return m_length ? m_tail->m_pred : m_tail;
	}

	inline ListNode* ReverseLast(void) const {
		return m_length ? m_head : m_tail;
	}

	inline int32_t Length(void) {
		return m_length;
	}

	inline const int32_t Length(void) const {
		return m_length;
	}

	inline const bool IsAvailable(void) const {
		return m_head != nullptr;
	}

	inline const bool IsEmpty(void) const {
		return m_length == 0;
	}

	inline ItemType& operator[] (int i) {
		ListNode* node = NodePtrAt(i);
		if (node) {
			m_result = true;
			return node->DataItem();
		}
		m_result = false;
		return *m_none;
	}
#if 0
	inline DataType& operator[] (ItemType& d) {
		int i = Find(d);
		if (i < 0)
			return *m_none;
		ListNodePtr p = NodePtrAt(int(i));
		return p ? p->DataValue() : *m_none;
	}
#endif
	inline List<ItemType>& operator= (List<ItemType> const& other) {
		Copy(other);
		return *this;
	}

	inline List<ItemType>& operator= (List<ItemType>&& other) noexcept {
		return Move(other);
	}

	inline List<ItemType>& operator= (std::initializer_list<ItemType> data) {
		Destroy();
		Init();
		for (auto& v : data)
			Append(v);
		return *this;
	}

	List<ItemType>(const char* name = "", int32_t segmentLength = 1)
		: m_name(name), m_length(0), m_result(true), m_isValid(false)
	{
		Init();
	}

	List<ItemType>(List<ItemType> const& other)
		: m_length(0), m_result(true), m_isValid(false)
	{
		Init();
		Copy(other);
	}

	List<ItemType>(List<ItemType>&& other) noexcept
		: m_length(0), m_result(true), m_isValid(false)
	{
		Init();
		Move(other);
	}

	explicit List<ItemType>(ItemType& data)
		: m_length(0), m_result(true), m_isValid(false)
	{
		Init();
		Append(data);
	}

	explicit List<ItemType>(Array<ItemType>& data, bool manageData = false)
		: m_length(0), m_result(true), m_isValid(false)
	{
		Init();
		for (auto const& v : data)
			Append(*v, manageData);
	}

	List<ItemType>(std::initializer_list<ItemType> data, bool manageData = false, int32_t segmentSize = 0)
		: m_length(0), m_result(true), m_isValid(false)
	{
		Init();
		for (auto const& d : data)
			Append(d, manageData);
	}

	~List() {
		Destroy();
	}

	//-----------------------------------------------------------------------------

public:
	inline ListNode* NodePtrAt(int i) {
		return NodePtrAt(i, m_headPtr + 1, m_tailPtr - 1);
	}


	ListNode* NodePtrAt(int i, ListNode* first, ListNode* last) {
		if (i == 0)
			return first;
		if (i == -1)
			return last;

		if (not IsAvailable() or (abs(i) > m_length - 1)) {
			m_result = false;
			return static_cast<ListNode*>(nullptr);
		}

		if (i > m_length / 2)
			i = -(int(m_length) - i);
		ListNodePtr p;
		if (i > 0) {
			for (p = first; (i > 0) and (p != m_tailPtr); i--)
				++p;
		}
		else {
			for (p = last; (++i < 0) and (p != m_headPtr); )
				--p;
		}
		m_result = i == 0;
		return m_result ? p.m_nodePtr : static_cast<ListNode*>(nullptr);
	}

	//-----------------------------------------------------------------------------

public:
	ListNode* AddNode(int i, ListNode* newNode = nullptr, bool manageData = false) {
		if (not IsAvailable())
			return nullptr;
		ListNode* insertBefore = NodePtrAt(i, m_headPtr + 1, m_tailPtr);
		if (not insertBefore)
			return nullptr;
		if (not newNode and (not (newNode = new ListNode())))
			return nullptr;
		newNode->m_pred = insertBefore->m_pred;
		insertBefore->m_pred->m_succ = newNode;
		newNode->m_succ = insertBefore;
		newNode->m_manageData = manageData;
		insertBefore->m_pred = newNode;
		m_length++;
		return newNode;
	}

	//-----------------------------------------------------------------------------

public:
	ItemType* Insert(int i, bool manageData = false) {
		ListNode* newNode = AddNode(i, nullptr, manageData);
		return newNode ? &newNode->DataItem() : nullptr;
	}

	template<typename T>
	ItemType* Insert(int i, T&& dataItem, bool manageData = false) {
		ItemType* itemPtr = Insert(i, manageData);
		if (not itemPtr)
			return nullptr;
		*itemPtr = std::forward<T>(dataItem);
		return itemPtr;
	}

	//-----------------------------------------------------------------------------

public:
	ItemType Extract(int i) {
		m_result = false;
		if (not m_length)
			return *m_none;

		ListNode* node = NodePtrAt(i, m_headPtr + 1, m_tailPtr - 1);
		if (not node)
			return *m_none;
		ItemType data = node->DataValue();
		delete node;
		m_length--;
		m_result = true;
		return data;
	}

	//-----------------------------------------------------------------------------

	bool Extract(ItemType& data, int i) {
		m_result = false;
		if (not m_length)
			return false;

		ListNode* node = NodePtrAt(i, m_headPtr + 1, m_tailPtr - 1);
		if (not node)
			return false;
		data = node->DataValue();
		delete node;
		m_length--;
		return true;
	}

	//-----------------------------------------------------------------------------

public:
	bool Discard(int i) {
		if (not m_length)
			return false;
		ListNode* node = NodePtrAt(i, m_headPtr + 1, m_tailPtr - 1);
		if (not node)
			return *m_none;
		delete node;
		m_length--;
		return m_result = true;
	}

	//-----------------------------------------------------------------------------
	// copy the other list to the end of this list
	// will leave other list intact

public:
	List<ItemType>& operator+= (const List<ItemType>& other) { // copy other to end of *this
		if (other.IsEmpty())
			return *this;
		for (auto& d : other)
			Insert(-1, d);
		return *this;
	}


	List<ItemType>& operator+= (List<ItemType>&& other) { // move other to end of *this
		if (other.IsEmpty())
			return *this;
		if (IsEmpty())
			return Move(other);
		ListNodePtr thisLast = ListNodePtr(Last());
		ListNodePtr otherFirst = ListNodePtr(other.First());
		thisLast.Succ() = otherFirst;
		otherFirst.Pred() = thisLast;
		std::swap(m_tail, other.m_tail);
		std::swap(m_tailPtr, other.m_tailPtr);
		other.m_headPtr.Succ() = other.m_tail;
		other.m_tailPtr.Pred() = other.m_head;
		return *this;
	}

	//-----------------------------------------------------------------------------
	// move the other list to this list. current list content will be removed first.
	// will leave other list empty

public:
	List<ItemType>& Move(List<ItemType>& other) {
		Destroy();
		if (not other.IsAvailable())
			Init();
		else {
			m_head = other.m_head;
			m_tail = other.m_tail;
			m_headPtr = other.m_head;
			m_tailPtr = other.m_tail;
			m_length = other.m_length;
			other.Reset();
		}
		return *this;
	}

	//-----------------------------------------------------------------------------

public:
	List<ItemType> operator+ (const List<ItemType>& other) {
		List<ItemType> l;
		if (IsEmpty())
			return l = other;
		if (other.IsEmpty())
			return l = *this;
		l = *this;
		l += other;
		return l;
	}

	//-----------------------------------------------------------------------------

public:
	template<typename T>
	int Find(T&& data) {
		ItemType pattern = std::forward<T>(data);
		for (auto it = begin(); it != end(); it++)
			if (*it == pattern)
				return int (it.Index());
		return -1;
	}

	//-----------------------------------------------------------------------------

public:
	List<ItemType> Splice(int32_t from, int32_t to = 0) {
		ListNode* start = NodePtrAt(int(from), m_headPtr + 1, m_tailPtr - 1);
		ListNode* end = NodePtrAt(int(to ? to : m_length - 1), m_headPtr + 1, m_tailPtr - 1);
		List<ItemType> l;
		if ((start != nullptr) and (end != nullptr)) {
			for (ListNode* it = start; it != end; it = it->next) {
				l.Append(new ListNode(it->m_dataItem, it->m_manageData));
			}
		}
		return l;
	}

	//-----------------------------------------------------------------------------

public:
	template<typename T>
	inline ItemType* Append(T&& dataItem, bool manageData = false) {
		return Insert(-1, std::forward<T>(dataItem), manageData);
	}

	inline ItemType* Append(bool manageData = false) {
		return Insert(-1, manageData);
	}

	//-----------------------------------------------------------------------------

public:
	inline bool Remove(ItemType data) {
		int i = Find(data);
		if (i < 0)
			return (m_result = false);
		Extract(int32_t(i));
		return (m_result = true);
	}

	//-----------------------------------------------------------------------------

public:
	template<typename FILTER_T>
	int32_t Filter(FILTER_T filter) {
		int32_t deleted = 0;
		for (ListNode* nodePtr = First(); nodePtr != Last(); ) {
			ListNode* candidate = nodePtr;
			nodePtr = nodePtr->Succ();
			if (filter(*candidate->DataPointer())) {
				delete candidate;
				deleted++;
			}
		}
		m_length -= deleted;
		return deleted;
	}

	//-----------------------------------------------------------------------------

	inline void Swap(ItemType* left, ItemType* right)
	{
		ItemType h;
		memcpy(&h, left, sizeof(ItemType));
		memcpy(left, right, sizeof(ItemType));
		memcpy(right, &h, sizeof(ItemType));
		memset(&h, 0, sizeof(ItemType));
	}

	//-----------------------------------------------------------------------------

	ItemType& GetMedian(ListNodePtr p, int i) {
		for (; i; i--)
			p++;
		return p->DataValue();
	}

	//-----------------------------------------------------------------------------

	void Sort(ListNodePtr leftNode, ListNodePtr rightNode, int32_t left, int32_t right, int direction)
	{
		int32_t		l = left,
					r = right;
		ListNodePtr	ln = leftNode,
					rn = rightNode;

		MemCpy(&m_median, GetMedian(left, (l + r) / 2), sizeof(ItemType));
		do {
			while (direction * m_compare(ln->DataPointer(), &m_median) < 0) {
				++l;
				++ln;
			}
			while (direction * m_compare(rn->DataPointer(), &m_median) > 0) {
				--r;
				--rn;
			}
			if (l <= r) {
				if (l < r)
					Swap(l->DataValue(), r->DataValue());
				l++;
				r--;
			}
		} while (l <= r);
		memset(&m_median, 0, sizeof(ItemType));

		if (l < right)
			Sort(ln, rightNode, l, right, direction);
		if (left < r)
			Sort(leftNode, rn, left, r, direction);
	}

	//-----------------------------------------------------------------------------

	void SortDescending(tComparator compare)
	{
		m_compare = compare;
		Sort(First(), Last(), m_length - 1, -1);
	}

	//-----------------------------------------------------------------------------

	void SortAscending(tComparator compare)
	{
		if (m_length > 1) {
			m_compare = compare;
			Sort(m_headPtr->Succ(), m_tailPtr->Pred(), m_length, -1);
		}
	}

	//-----------------------------------------------------------------------------

public:
	inline bool Result(void) {
		return m_result;
	}

	//-----------------------------------------------------------------------------

};
