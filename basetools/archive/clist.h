#pragma once

#include <utility>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <type_traits>
#include <cstddef>
#include <utility>
#include <iostream>
#include "array.hpp"
#include "sharedpointer.hpp"
#include "allocator.h"

//-----------------------------------------------------------------------------

template < class DATA_T >
class List 
#if DEBUG_MALLOC 
	: public Allocator
#endif
{

	private:
		class ListNode;

		//----------------------------------------

		class ListNodePtr {
			public:
				ListNode* m_nodePtr;

				explicit ListNodePtr () : m_nodePtr (nullptr) {}
/*
				ListNodePtr(const ListNodePtr& other) {
					m_nodePtr = other.m_nodePtr;
				}
*/
				explicit ListNodePtr(ListNode * nodePtr) : m_nodePtr(nodePtr) {}

				ListNodePtr& operator= (ListNodePtr other) {
					m_nodePtr = other.m_nodePtr;
					return *this;
				}

				ListNodePtr& operator= (ListNode * p) {
					m_nodePtr = p;
					return *this;
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
					ListNodePtr nodePtr = *this;
					for (; n; n--)
						++nodePtr;
					return nodePtr;
				}

				ListNodePtr operator- (int n) {
					ListNodePtr nodePtr = *this;
					for (; n; n--)
						--nodePtr;
					return nodePtr;
				}

				ListNode* operator->() const {
					return m_nodePtr;
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

				operator bool() {
					return (m_nodePtr != nullptr);
				}

				DATA_T& DataValue(void) {
					return m_nodePtr->DataValue();
				}

				DATA_T& DataValue() const {
					return m_nodePtr->DataValue();
				}

				DATA_T* DataPointer() {
					return m_nodePtr->DataPointer();
				}

		};

		//----------------------------------------

		class ListNode 
#if DEBUG_MALLOC 
			: public Allocator
#endif
		{
			public:
				ListNodePtr	m_pred;
				ListNodePtr	m_succ;
				DATA_T		m_data;

				explicit ListNode () : m_pred (nullptr), m_succ (nullptr) {}

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

				~ListNode() {
					Unlink();
					if constexpr (std::is_pointer<DATA_T>::value) {
						if (m_data)
							delete m_data;
						}
					}

				DATA_T& DataValue(void) {
					return m_data;
					}

				DATA_T& DataValue() const {
					return m_data;
				}

				DATA_T* DataPointer() {
					return &m_data;
				}
		};

		//----------------------------------------

		class Iterator {

			private:
				ListNodePtr 	m_start;
				ListNodePtr 	m_end;
				ListNodePtr 	m_current;
				size_t			m_index;
				size_t			m_length;

			public:
				explicit Iterator() {}

				Iterator(List< DATA_T >& l) : m_start(l.First ()), m_end(l.Last () + 1), m_index (0), m_length(l.Length()) {}

				inline operator bool() const { return m_current != nullptr; }

				//DATA_T* operator*() const { return &m_current->m_data; }
				// return the current value and the index of the value in the list
				DATA_T& operator*() const { 
					return m_current->DataValue();
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

				bool inline operator== (const Iterator& other) const {
					return m_current == other.m_current;
				}

				bool inline operator!= (const Iterator& other) const {
					return m_current != other.m_current;
				}

				//Iterator& Start(void) {
				constexpr Iterator& Start(void) {
					m_index = 0;
					m_current = m_start;
					return *this;
				}

				inline Iterator& End(void) {
					m_current = m_end;
					m_index = m_length;
					return *this;
				}

				inline size_t Index(void) {
					return m_index;
				}
			};

	// ----------------------------------------

	private:
		ListNode		m_head;
		ListNode		m_tail;
		DATA_T			m_none;
		ListNodePtr		m_headPtr;
		ListNodePtr 	m_tailPtr;
		ListNodePtr 	m_nullPtr;
		size_t			m_length;
		bool			m_result;

	public:
		void Destroy(void) {
			if (m_headPtr) {
				ListNodePtr p, n;
				for (n = m_headPtr + 1; n != m_tailPtr; ) {
					p = n;
					n++;
					if (p.m_nodePtr) {
						delete p.m_nodePtr;
						p.m_nodePtr = nullptr;
					}
				}
			}
		}

		void Clear(void) {
			Destroy();
			Init();
		}

		List<DATA_T>& Copy(const List<DATA_T> & other) {
			Clear();
			ListNodePtr p = other.m_headPtr;
			for (p++; p != other.m_tailPtr; p++)
				Insert (-1, p.DataValue());
			return *this;
		}

		inline void Init(void) {
			m_headPtr = &m_head;
			m_tailPtr = &m_tail;
			m_headPtr->m_pred = m_tailPtr->m_succ = nullptr;
			m_headPtr->m_succ = &m_tail;
			m_tailPtr->m_pred = &m_head;
#if !DEBUG_MALLOC
			if constexpr (std::is_trivially_constructible<DATA_T>::value)
				new(&m_none) DATA_T();
			else
#endif
				memset(&m_none, 0, sizeof(m_none));
			m_length = 0;
		}

		Iterator begin() {
			return Iterator(*this).Start();
		}

		Iterator end() {
			return Iterator(*this).End();
		}

		inline ListNodePtr Head(void) {
			return m_headPtr;
		}

		inline ListNodePtr Tail(void) {
			return m_tailPtr;
		}

		inline ListNodePtr First(void) {
			return m_head.m_succ;
		}

		inline ListNodePtr Last(void) {
			return m_tail.m_pred;
		}

		inline size_t Length(void) {
			return m_length;
		}

		inline const bool IsEmpty(void) const {
			return m_length == 0;
		}

		inline DATA_T& operator[] (const size_t i) {
			ListNodePtr p = NodePtrAt(int (i), m_headPtr + 1, m_tailPtr - 1);
			if (p) {
				m_result = true;
				return p->DataValue();
			}
			m_result = true;
			return m_none;
		}
#if 0
		inline DATA_T& operator[] (DATA_T& d) {
			int i = Find(d);
			if (i < 0)
				return m_none;
			ListNodePtr p = NodePtrAt(int(i), m_headPtr + 1, m_tailPtr - 1);
			return p ? p->DataValue() : m_none;
		}
#endif
		inline List<DATA_T>& operator= (List<DATA_T> const& other) {
			Copy(other);
			return *this;
		}

		inline List<DATA_T>& operator= (List<DATA_T>&& other) noexcept {
			return Move (other);
		}

		inline List<DATA_T>& operator= (std::initializer_list<DATA_T> data) {
			Clear();
			for (auto const& d : data)
				Append(d);
			return *this;
		}

		List(size_t segmentLength = 1) 
			: m_length (0), m_result (true) 
		{
			Init();
		}

		List(List<DATA_T> const& other) 
			: m_length (0), m_result (true) 
		{
			Init();
			Copy(other);
		}
		
		List(List<DATA_T>&& other) noexcept 
			: m_length (0), m_result (true) 
		{
			Init ();
			Move(other);
		}

		explicit List(DATA_T& data) 
			:  m_length(0), m_result(true) 
		{
			Append(data);
		}

		explicit List(Array<DATA_T>& data) 
			:  m_length (0), m_result (true) 
		{
			Init ();
			for (auto const& v : data)
				Append(*v);
		}

		List(std::initializer_list<DATA_T> data, size_t segmentSize = 0) 
			: m_length (0), m_result (true) {
			Init ();
			for (auto const& d : data)
				Append(d);
		}

		inline DATA_T& DataValue(ListNode* nodePtr) {
			return nodePtr->DataValue();
		}

		inline DATA_T* DataPointer(ListNode* nodePtr) {
			return nodePtr->DataPointer();
		}

		~List() {
			Destroy();
		}

//-----------------------------------------------------------------------------

private:
ListNodePtr NodePtrAt(int i, ListNodePtr first, ListNodePtr last) {
	if (i == 0)
		return first;
	if (i == -1)
		return last;
	if (abs(i) > m_length - 1)
		return m_nullPtr;
	if (i > m_length / 2)
		i = -(int(m_length) - i);
	ListNodePtr p;
	if (i > 0) {
		for (p = first; (i > 0) && (p != m_tailPtr); i--)
			++p;
	}
	else {
		for (p = last; (++i < 0) && (p != m_headPtr); )
			--p;
	}
	return i ? m_nullPtr : p;

}

//-----------------------------------------------------------------------------

public:
ListNode* Add(int i) {
	ListNodePtr p = NodePtrAt(i, m_headPtr + 1, m_tailPtr);
	if (p == m_nullPtr)
		return nullptr;
	ListNode* n = new ListNode;
	if (!n)
		return nullptr;
	n->m_pred = p->m_pred;
	n->m_pred->m_succ = n;
	n->m_succ = p;
	p->m_pred = n;
	m_length++;
	return n;
}

//-----------------------------------------------------------------------------

public:
template<typename T>
bool Insert (int i, T&& data) {
	ListNode* nodePtr = Add (i);
	if (!nodePtr)
		return false;
	nodePtr->m_data = std::forward<T>(data);
	return true;
}

//-----------------------------------------------------------------------------

public:
DATA_T Extract(int i) {
	m_result = false;
	if (!m_length)
		return DATA_T (m_none);

	ListNodePtr nodePtr = NodePtrAt(i, m_headPtr + 1, m_tailPtr - 1);
	if (!nodePtr)
		return DATA_T (m_none);
	ListNode* p = nodePtr.m_nodePtr;
	DATA_T data = std::move(p->m_data);
	delete p;
	m_length--;
	m_result = true;
	return data;
}

//-----------------------------------------------------------------------------
// move the other list to the end of this list
// will leave other list empty

public:
List< DATA_T >& operator+= (List< DATA_T> other) {
	if (other.IsEmpty())
		return *this;
#if 1
	for (auto d : other)
		Insert (-1, d);
#else
	ListNode* s = other.m_head.m_succ.m_nodePtr;
	ListNode* e;
	if (IsEmpty()) {
		m_headPtr.m_nodePtr->m_succ = s;
		s->m_pred = m_headPtr;
	}
	else {
		e = m_tailPtr.m_nodePtr->m_pred.m_nodePtr;
		e->m_succ = s;
		s->m_pred = e;
	}
	e = other.m_tail.m_pred.m_nodePtr;
	e->m_succ = &m_tail;
	m_tail.m_pred = e;
	m_tailPtr = &m_tail;
	other.Init ();
#endif
	return *this;
}

//-----------------------------------------------------------------------------
// move the other list to the end of this list
// will leave other list empty

public:
List< DATA_T >& Move (List< DATA_T >& other) {
	Clear ();
	if (other.IsEmpty ())
		return *this;
	ListNode* s = other.m_head.m_succ.m_nodePtr;
	ListNode* e;
	if (IsEmpty ()) {
		m_headPtr.m_nodePtr->m_succ = s;
		s->m_pred = m_headPtr;
	}
	else {
		e = m_tailPtr.m_nodePtr->m_pred.m_nodePtr;
		e->m_succ = s;
		s->m_pred = e;
	}
	e = other.m_tail.m_pred.m_nodePtr;
	e->m_succ = m_tailPtr;
	m_tailPtr->m_pred = e;
	m_length = other.m_length;
	other.Init ();
	return *this;
}

//-----------------------------------------------------------------------------

public:
List< DATA_T > operator+ (const List< DATA_T >& other) {
	List< DATA_T > l;
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
	DATA_T pattern = std::forward<T>(data);
	for (auto p : *this)
		if (p == pattern)
			return int (i);
	return -1;
}

//-----------------------------------------------------------------------------

public:
List< DATA_T > Splice(size_t from, size_t to) {
	ListNode* start = NodePtrAt(int (from));
	ListNode* end = NodePtrAt(int (to ? to : m_length - 1));
	List< DATA_T > l;
	if ((start != nullptr) && (end != nullptr)) {
		for (ListNode* i = start; i != end; i = i->next) {
			ListNode& n = l.Append(new ListNode);
			*n.m_data = *i.m_data;
		}
	}
	return l;
}

//-----------------------------------------------------------------------------

public:
template <typename T>
inline bool Append(T&& data) {
	return Insert(-1, std::forward<T>(data));
}

//-----------------------------------------------------------------------------

public:
	template <typename T>
	inline bool Remove(T&& data) {
	int i = Find(std::forward<T>(data));
	if (i < 0)
		return (m_result = false);
	Extract(size_t(i));
	return (m_result = true);
}

//-----------------------------------------------------------------------------

public:
inline bool Result(void) {
	return m_result;
}

//-----------------------------------------------------------------------------

};

