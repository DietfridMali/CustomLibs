#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>

#include "quicksort.hpp"

#define sizeofa(_a)	((sizeof(_a) / sizeof(*(_a))))

//-----------------------------------------------------------------------------

template < class DATA_T > 
class Array : public QuickSort < DATA_T > {

	// ----------------------------------------

	class ArrayInfo {
		public:
			DATA_T	none;
			size_t	capacity;
			size_t	height;
			size_t	width;
			size_t	pos;
			size_t	offset;
			size_t	mode;
			bool	wrap;

		public:

			ArrayInfo() : capacity(0), height(0), width(0), pos(0), offset(0), mode(0), wrap(false)
			{
				memset(&none, 0, sizeof(none));
			}

			inline size_t Capacity (void) { return capacity; }
		};

	protected:
		ArrayInfo		m_info;
		DATA_T*			m_data;
		DATA_T			m_none;

		// ----------------------------------------

	public:
		class Iterator {
			private:
				DATA_T *		m_start;
				DATA_T *		m_end;
				DATA_T *		m_current;

			public:
				explicit Iterator () : m_start (nullptr), m_end (nullptr), m_current (nullptr) {}

				Iterator (Array& a) : m_start (a.Start ()), m_end (a.End () + 1), m_current (nullptr) {}

				operator bool() const { 
					return m_current != nullptr; 
				}

				DATA_T* operator*() const { 
					return m_current; 
				}

				Iterator& operator++() { 
					++m_current;
					return *this;
				}

				Iterator& operator--() { 
					--m_current;
					return *this;
				}

				Iterator& Start (void) {
					m_current = m_start;
					return *this;
				}

				Iterator& End (void) {
					m_current = m_end;
					return *this;
				}

				bool operator== (Iterator& other) {
					return m_current == other.m_current;
				}

				bool operator!= (Iterator& other) {
					return m_current != other.m_current;
				}

		};

		// ----------------------------------------

		Array() 
			: m_data(nullptr)
		{ 
			Init (); 
		}
		
		explicit Array(const size_t capacity) 
			: m_data(nullptr)
		{
			Reserve (capacity);
		}
		
		explicit Array(const size_t height, const size_t width) 
			: m_data(nullptr)
		{
			Reserve(width * height);
			Init(width, height);
		}

		Array(Array const& other) 
			: m_data(nullptr)
		{
			Init (); 
			Copy (other);
		}
		
		Array (Array&& other) 
			: m_data(nullptr)
		{
			Move (other);
		}

		explicit Array(DATA_T const* data, size_t capacity, size_t offset = 0) 
			: m_data(nullptr)
		{
			Reserve(capacity);
			Init(offset);
			memcpy(m_data, data, sizeof(DATA_T) * capacity);
		}

		Array(std::initializer_list<DATA_T> data) 
			: m_data(nullptr)
		{
			Reserve(data.size());
			Init();
			size_t i = 0;
			for (auto it = data.begin(); it != data.end(); it++)
				*Data(i++) = *it;
			//memcpy(m_data, data.begin(), sizeof(DATA_T) * data.size());
		}

		~Array() {
			Destroy (); 
		}
		
		Iterator begin() {
			return Iterator (*this).Start();
		}

		Iterator end() {
			return Iterator (*this).End();
		}

		// ----------------------------------------

		void Init(size_t height = 0, size_t width = 0) {
			m_info.capacity = 0;
			m_info.height = height;
			m_info.width = width;
			m_info.offset = 0;
			m_info.pos = 0;
			m_info.mode = 0;
			m_info.wrap = false;
			memset(&m_none, 0, sizeof(m_none));
		}

		// ----------------------------------------

		void Clear (uint8_t filler = 0, size_t count = 0xffffffff) { 
			if (m_data) 
				memset (m_data, filler, sizeof (DATA_T) * ((count < m_info.capacity) ? count : m_info.capacity)); 
			}

		// ----------------------------------------

		void Fill (DATA_T filler, size_t count = 0xffffffff) {
			if (m_data) {
				if (count > m_info.capacity)
					count = m_info.capacity;
				for (DATA_T* bufP = Data(); count; count--, bufP++)
					*bufP = filler;
			}
		}

		// ----------------------------------------

		inline bool IsIndex (size_t i) { 
			return (m_data != nullptr) && (i < m_info.capacity); 
		}
		
		// ----------------------------------------

		inline bool IsElement (DATA_T* elem, bool bDiligent = false) {
			if (!m_data || (elem < m_data) || (elem >= m_data + m_info.capacity))
				return false;	// no data or element out of data
			if (bDiligent) {
				size_t i = static_cast<size_t> (reinterpret_cast<uint8_t*> (elem) - reinterpret_cast<uint8_t*> (m_data));
				if (i % sizeof (DATA_T))	
					return false;	// elem in the data, but not properly aligned
			}
			return true;
		}

		// ----------------------------------------

		inline size_t Index (DATA_T* elem) { 
			return size_t (elem - m_data); 
		}

		// ----------------------------------------

		inline DATA_T* Pointer (size_t i) { 
			return m_data + i; 
		}

		// ----------------------------------------

		void Destroy (void) { 
			if (m_data) {
				if (!m_info.mode)
					delete[] m_data;
				Init ();
			}
		}
			
		// ----------------------------------------

		DATA_T *Reserve (size_t capacity) {
			if (m_info.capacity != capacity) {
				Destroy ();
				if ((m_data = new DATA_T [capacity]))
					m_info.capacity = capacity;
			}
			return m_data;
		}
			
		// ----------------------------------------

		inline DATA_T* Data (size_t i = 0) const { 
			return m_data + i; 
		}
		
		// ----------------------------------------

		void SetBuffer (const DATA_T *data, size_t mode = 0, size_t capacity = 0xffffffff) {
			if (m_data != data) {
				if (!(m_data = data))
					Init ();
				else {
					m_info.capacity = capacity;
					m_info.mode = mode;
				}
			}
		}
			
		// ----------------------------------------

		DATA_T* Resize (size_t capacity, bool bCopy = true) {
			if (m_info.mode == 2)
				return m_data;
			if (!m_data)
				return Reserve (capacity);
			DATA_T* p;
			try {
				p = new DATA_T [capacity];
			}
			catch(...) {
				return m_data;
			}
			if (bCopy) {
				memcpy (p, m_data, ((capacity > m_info.capacity) ? m_info.capacity : capacity) * sizeof (DATA_T)); 
				Clear (); // hack to avoid d'tors
			}
			m_info.capacity = capacity;
			m_info.pos %= capacity;
			delete[] m_data;
			return m_data = p;
		}

		// ----------------------------------------

		inline const size_t Capacity (void) const { 
			return m_info.capacity; 
		}

		// ----------------------------------------

		inline DATA_T* Current (void) { 
			return m_data ? m_data + m_info.pos : nullptr; 
		}

		// ----------------------------------------

		inline size_t Size (void) { 
			return m_info.capacity * sizeof (DATA_T); 
		}


		// ----------------------------------------

		inline bool IsValidIndex(size_t i) {
			return (i >= 0) && (i < m_info.capacity);
		}

		// ----------------------------------------

		inline bool IsValidIndex(size_t x, size_t y) {
			return (x < m_info.width) && (y < m_info.height);
		}

		// ----------------------------------------

		inline int GetCheckedIndex(size_t x, size_t y) {
			return ((x > m_info.width) || (y > m_info.height)) ? -1 : int(y * m_info.width + x);
		}

		// ----------------------------------------

		inline DATA_T& operator[] (const size_t i) {
			if (IsValidIndex(i))
				return m_data [i]; 
			return m_none;
		}

		// ----------------------------------------

		inline DATA_T& operator()(size_t x, size_t y) {
			int i = GetCheckedIndex(x, y);
			if (i < 0)
				return m_none;
			return *Data(i);
		}

		// ----------------------------------------

		DATA_T* GetRow(size_t y) {
			return Data(y * m_info.width);
		}

		// ----------------------------------------

		inline DATA_T& operator()(size_t x, size_t y, bool rangeCheck) { // always checks range; parameter only to distinguish from other operator()
			int i = GetCheckedIndex(x, y);
			if (i < 0)
				return m_none;
			return *Data(i);
		}

		// ----------------------------------------

		inline DATA_T* operator* () const { 
			return m_data; 
		}

		// ----------------------------------------

		inline Array<DATA_T>& operator= (Array<DATA_T> const & source) {
			return _Copy (source.Data (), source.Capacity ()); 
		}

		// ----------------------------------------

		inline Array<DATA_T>& operator= (Array<DATA_T>&& source) {
			return Move (source);
		}

		// ----------------------------------------

		inline Array<DATA_T>& operator= (std::initializer_list<DATA_T> data) {
			Init();
			Reserve(data.size());
			memcpy(m_data, data.begin(), sizeof(DATA_T) * data.size());
			return *this;
		}

		// ----------------------------------------

		inline DATA_T& operator= (DATA_T* source) { 
			memcpy (m_data, source, m_info.capacity * sizeof (DATA_T)); 
			return m_data [0];
		}

		// ----------------------------------------

		inline Array<DATA_T>& Copy (Array<DATA_T> const & source, size_t offset = 0) { 
			return _Copy(source.Data(), source.Capacity(), offset);
		}

		// ----------------------------------------

		Array<DATA_T>& _Copy(DATA_T const* source, size_t capacity, size_t offset = 0) {
			if ((m_data && (m_info.capacity >= capacity + offset)) || Resize(capacity + offset, false))
				memcpy(m_data + offset, source, ((m_info.capacity - offset < capacity) ? m_info.capacity - offset : capacity) * sizeof(DATA_T));
			return *this;
		}

		// ----------------------------------------

		Array<DATA_T>& Move (Array<DATA_T>& source) {
			Destroy ();
			m_data = source.m_data;
			m_info.capacity = source.m_info.capacity;
			m_info.pos = source.m_info.pos;
			m_info.mode = source.m_info.mode;
			m_info.wrap = source.m_info.wrap;
			source.m_data = nullptr;
			source.Destroy ();
			return *this;
		}

		// ----------------------------------------

		inline DATA_T operator+ (Array<DATA_T>& source) { 
			Array<DATA_T> a (*this);
			a += source;
			return a;
		}

		// ----------------------------------------

		inline DATA_T& operator+= (Array<DATA_T>& source) { 
			size_t offset = m_info.capacity;
			if (m_data) 
				Resize (m_info.capacity + source.m_info.capacity);
			return Copy (source, offset);
		}

		// ----------------------------------------

		inline bool operator== (Array<DATA_T>& other) { 
			return (m_info.capacity == other.m_info.capacity) && !(m_info.capacity && memcmp (m_data, other.m_data)); 
		}

		// ----------------------------------------

		inline bool operator!= (Array<DATA_T>& other) { 
			return (m_info.capacity != other.m_info.capacity) || (m_info.capacity && memcmp (m_data, other.m_data)); 
		}

		// ----------------------------------------

		inline DATA_T* Start (void) { return m_data; }

		// ----------------------------------------

		inline DATA_T* End (void) { return (m_data && m_info.capacity) ? m_data + m_info.capacity - 1 : nullptr; }

		// ----------------------------------------

		inline DATA_T* operator++ (void) { 
			if (!m_data)
				return nullptr;
			if (m_info.pos < m_info.capacity - 1)
				m_info.pos++;
			else if (m_info.wrap) 
				m_info.pos = 0;
			else
				return nullptr;
			return m_data + m_info.pos;
		}

		// ----------------------------------------

		inline DATA_T* operator-- (void) { 
			if (!m_data)
				return nullptr;
			if (m_info.pos > 0)
				m_info.pos--;
			else if (m_info.wrap)
				m_info.pos = m_info.capacity - 1;
			else
				return nullptr;
			return m_data + m_info.pos;
		}

		// ----------------------------------------

		inline DATA_T* operator+ (size_t i) { 
			return m_data ? m_data + i : nullptr; 
		}

		// ----------------------------------------

		inline DATA_T* operator- (size_t i) { return m_data ? m_data - i : nullptr; }

		// ----------------------------------------

		Array<DATA_T>& ShareBuffer (Array<DATA_T>& child) {
			memcpy (&child.m_info, &m_info, sizeof (m_info));
			if (!child.m_info.mode)
				child.m_info.mode = 1;
			return child;
			}

		// ----------------------------------------

		inline bool operator! () { return m_data == nullptr; }

		// ----------------------------------------

		inline size_t Pos (void) { return m_info.pos; }

		// ----------------------------------------

		inline void Pos (size_t pos) { m_info.pos = pos % m_info.capacity; }

		// ----------------------------------------
#if 0
		size_t Read (CFile& cf, size_t nCount = 0, size_t nOffset = 0, bool bCompressed = 0) { 
			if (!m_data)
				return -1;
			if (nOffset >= m_info.capacity)
				return -1;
			if (!nCount)
				nCount = m_info.capacity - nOffset;
			else if (nCount > m_info.capacity - nOffset)
				nCount = m_info.capacity - nOffset;
			return cf.Read (m_data + nOffset, sizeof (DATA_T), nCount, bCompressed);
			}

		// ----------------------------------------

		size_t Write (CFile& cf, size_t nCount = 0, size_t nOffset = 0, size_t bCompressed = 0) { 
			if (!m_data)
				return -1;
			if (nOffset >= m_info.capacity)
				return -1;
			if (!nCount)
				nCount = m_info.capacity - nOffset;
			else if (nCount > m_info.capacity - nOffset)
				nCount = m_info.capacity - nOffset;
			return cf.Write (m_data + nOffset, sizeof (DATA_T), nCount, bCompressed);
			}
#endif

		// ----------------------------------------

		inline void SetWrap (bool wrap) { m_info.wrap = wrap; }

		// ----------------------------------------

		inline void SortAscending (size_t left = 0, size_t right = 0) { 
			if (m_data) 
				QuickSort<DATA_T>::SortAscending (m_data, left, (right > 0) ? right : m_info.capacity - 1); 
				}

		// ----------------------------------------

		inline void SortDescending (size_t left = 0, size_t right = 0) {
			if (m_data) 
				QuickSort<DATA_T>::SortDescending (m_data, left, (right > 0) ? right : m_info.capacity - 1);
			}

		// ----------------------------------------

		inline void SortAscending (QuickSort<DATA_T>::tComparator compare, size_t left = 0, size_t right = 0) {
			if (m_data) 
				QuickSort<DATA_T>::SortAscending (m_data, left, (right > 0) ? right : m_info.capacity - 1, compare);
			}

		// ----------------------------------------

		inline void SortDescending (QuickSort<DATA_T>::tComparator compare, size_t left = 0, size_t right = 0) {
			if (m_data) 
				QuickSort<DATA_T>::SortDescending (m_data, left, (right > 0) ? right : m_info.capacity - 1, compare);
			}

		// ----------------------------------------

		// ----------------------------------------

		template<typename KEY_T>
		inline size_t Find(KEY_T const& key, int(__cdecl* compare) (DATA_T const&, KEY_T const&), size_t left = 0, size_t right = 0) {
			return m_data ? this->BinSearch(Data(), key, compare, left, (right > 0) ? right : m_info.capacity - 1) : -1;
		}
	};

//-----------------------------------------------------------------------------

inline size_t operator- (char* v, Array<char>& a) { return a.Index (v); }
inline size_t operator- (uint8_t* v, Array<uint8_t>& a) { return a.Index (v); }
inline size_t operator- (int16_t* v, Array<int16_t>& a) { return a.Index (v); }
inline size_t operator- (uint16_t* v, Array<uint16_t>& a) { return a.Index (v); }
inline size_t operator- (uint32_t* v, Array<uint32_t>& a) { return a.Index (v); }
inline size_t operator- (size_t* v, Array<size_t>& a) { return a.Index (v); }

//-----------------------------------------------------------------------------

class CharArray : public Array<char> {
	public:
	using Array<char>::Array;

	inline char* operator= (const char* source) {
			size_t l = size_t (strlen (source) + 1);
			if ((l > this->m_info.capacity) && !this->Resize (this->m_info.capacity + l))
				return nullptr;
			memcpy (this->m_data, source, l);
			return this->m_data;
		}
};

//-----------------------------------------------------------------------------

class ByteArray : public Array<uint8_t> {
public:
	using Array<uint8_t>::Array;
};

class ShortArray : public Array<int16_t> {
public:
	using Array<int16_t>::Array;
};

class UShortArray : public Array<uint16_t> {
public:
	using Array<uint16_t>::Array;
};

class IntArray : public Array<int32_t> {
public:
	using Array<int32_t>::Array;
};

class UIntArray : public Array<uint32_t> {
public:
	using Array<uint32_t>::Array;
};

class SizeArray : public Array<size_t> {
public:
	using Array<size_t>::Array;
};

class FloatArray : public Array<float> {
public:
	using Array<float>::Array;
};

//-----------------------------------------------------------------------------

template < class DATA_T, size_t capacity > 
class StaticArray : public Array < DATA_T > {


	protected:
		DATA_T		m_data[capacity];

	public:
		StaticArray () { Reserve (capacity); }

		DATA_T *Reserve (size_t _length) {
			this->SetBuffer (m_data, 2, _length); 
			return m_data;
			}
		void Destroy (void) { }
	};

//-----------------------------------------------------------------------------
