// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#define NOMINMAX

#include "sharedpointer.hpp"
#include "quicksort.hpp"

#define sizeofa(_a)	((sizeof(_a) / sizeof(*(_a))))


// =================================================================================================

template<typename DATA_T, typename POINTER_T = DATA_T*>
class ArrayBuffer {
protected:
	POINTER_T	m_handle = nullptr;
	bool		m_isStatic = false;

public:
	ArrayBuffer() = default;

	explicit ArrayBuffer(POINTER_T handle, bool isStatic = false)
		: m_handle(handle), m_isStatic(isStatic)
	{
	}

	// Zuweisung eines rohen Zeigers oder eines SharedPointers
	ArrayBuffer& operator=(POINTER_T p) {
		m_handle = p;
		return *this;
	}

	// Cast in DATA_T*, z. B. für Arrayzugriff
	operator DATA_T* () const {
		if constexpr (std::is_pointer_v<POINTER_T>) {
			return m_handle;
		}
		else {
			return static_cast<DATA_T*>(m_handle); // erwartet: SharedPointer<DATA_T>::operator DATA_T*()
		}
	}

	void SetBuffer(DATA_T* data, bool isStatic) {
		if constexpr (std::is_pointer_v<POINTER_T>) {
			m_handle = data;
		}
		else {
			m_handle = POINTER_T(data, true, isStatic);
		}
		m_isStatic = isStatic;
	}

	inline POINTER_T& BufferHandle(void) {
		return m_handle;
	}

	inline const POINTER_T& BufferHandle(void) const {
		return m_handle;
	}

	// Optionaler []-Zugriff
	DATA_T& operator[](int32_t i) {
		return operator DATA_T * ()[i];
	}

	const DATA_T& operator[](int32_t i) const {
		return operator DATA_T * ()[i];
	}

	void Destroy() {
		if constexpr (std::is_pointer_v<POINTER_T>) {
			if (m_handle) {
				if (not m_isStatic)
					delete[] m_handle;
				m_handle = nullptr;
			}
		}
		else {
			m_handle.Release();
			m_isStatic = false;
		}
	}

	inline bool IsStatic(void) const {
		return !operator DATA_T * () or m_isStatic;
	}

	~ArrayBuffer() {
		Destroy();
	}
};

// =================================================================================================

template < typename DATA_T, typename POINTER_T = DATA_T* >
class ManagedArray
	: public ArrayBuffer<DATA_T, POINTER_T>
	, public QuickSort < DATA_T >
#if DEBUG_MALLOC 
	, public Allocator
#endif
{
public:
	using Base = ArrayBuffer<DATA_T, POINTER_T>;
	using Base::operator=; // erlaubt Zuweisung über m_handle = ...
	using Base::IsStatic;
	using Base::BufferHandle;
	using HANDLE_T = DATA_T*;

private:

	// ----------------------------------------

	class ArrayInfo {
	public:
		int32_t	capacity;
		int32_t	height;
		int32_t	width;
		int32_t	pos;
		int32_t	offset;
		bool	wrap;

	public:

		ArrayInfo(int32_t _width = 0, int32_t _height = 0, int32_t _offset = 0)
			: capacity(0), height(_height), width(_width), pos(0), offset(_offset), wrap(false)
		{
		}

		inline int32_t Capacity(void) { return capacity; }
	};

protected:
	const char* m_name;
	ArrayInfo				m_info;
	//ArrayBuffer<DATA_T	m_handle;
	DATA_T					m_none;

	// ----------------------------------------

public:
	class Iterator {
	private:
		DATA_T* m_start;
		DATA_T* m_end;
		DATA_T* m_current;

	public:
		explicit Iterator() : m_start(nullptr), m_end(nullptr), m_current(nullptr) {}

		Iterator(ManagedArray& a) : m_start(a.Start()), m_end(a.End() + 1), m_current(nullptr) {}

		operator bool() const {
			return m_current != nullptr;
		}

		DATA_T& operator*() const {
			return *m_current;
		}

		Iterator& operator++() {
			++m_current;
			return *this;
		}

		Iterator& operator--() {
			--m_current;
			return *this;
		}

		Iterator& First(void) {
			m_current = m_start;
			return *this;
		}

		Iterator& Last(void) {
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

	ManagedArray(const char* name = "")
		: m_name(name), m_info(), m_none(DATA_T())
	{
		// fprintf(stderr, "%s\n", __FUNCSIG__);
	}

	explicit ManagedArray(const int32_t capacity)
		: m_info(), m_none(DATA_T())
	{
		// fprintf(stderr, "%s\n", __FUNCSIG__);
		Reserve(capacity);
	}

	explicit ManagedArray(const int32_t width, const int32_t height)
		: m_info(width, height), m_none(DATA_T())
	{
		// fprintf(stderr, "%s\n", __FUNCSIG__);
		Reserve(width, height);
	}

	ManagedArray(ManagedArray const& other)
		: m_info(), m_none(DATA_T())
	{
		// fprintf(stderr, "%s\n", __FUNCSIG__);
		CopyData(other);
	}

	ManagedArray(ManagedArray&& other)
		: m_info(), m_none(DATA_T())
	{
		// fprintf(stderr, "%s\n", __FUNCSIG__);
		Move(other);
	}

	explicit ManagedArray(DATA_T const* data, int32_t capacity, int32_t offset = 0)
		: m_info(0, 0, offset), m_none(DATA_T())
	{
		// fprintf(stderr, "%s\n", __FUNCSIG__);
		Reserve(capacity);
		memcpy(Data(), data, sizeof(DATA_T) * capacity);
	}

	ManagedArray(std::initializer_list<DATA_T> data)
		: m_info(), m_none(DATA_T())
	{
		// fprintf(stderr, "%s\n", __FUNCSIG__);
		Reserve(int32_t(data.size()));
		int32_t i = 0;
		for (auto it = data.begin(); it != data.end(); it++)
			*Data(i++) = *it;
		//memcpy(m_handle, data.begin(), sizeof(DATA_T) * data.size());
	}

	~ManagedArray() {
		Destroy();
	}

	Iterator begin() {
		return Iterator(*this).First();
	}

	Iterator end() {
		return Iterator(*this).Last();
	}

	// ----------------------------------------

	void Init(int32_t width = -1, int32_t height = -1) {
		if (height != -1)
			m_info.height = height;
		if (width != -1)
			m_info.width = width;
		m_info.offset = 0;
		m_info.pos = 0;
		m_info.wrap = false;
		if constexpr (std::is_trivially_constructible<DATA_T>::value)
			memset(&m_none, 0, sizeof(m_none));
		else
			new(&m_none) DATA_T();
	}

	// ----------------------------------------

	void Reset(void) {
		Base::Destroy();
		m_info.capacity = 0;
		Init(); // leave width and height intact
	}

	// ----------------------------------------

	void Clear(uint8_t filler = 0, int32_t count = 0u) {
		if (Data())
			memset(Data(), filler, sizeof(DATA_T) * ((count and (count < m_info.capacity)) ? count : m_info.capacity));
	}

	// ----------------------------------------

	void Fill(DATA_T filler, int32_t count = -1) {
		if (Data()) {
			if (count < 0)
				count = m_info.capacity;
			for (DATA_T* bufP = Data(); count; count--, bufP++)
				*bufP = filler;
		}
	}

	// ----------------------------------------

	inline bool IsIndex(int32_t i) {
		return Data() and (i - m_info.offset >= 0) and (i - m_info.offset < m_info.capacity);
	}

	// ----------------------------------------

	inline bool IsElement(DATA_T* elem, bool bDiligent = false) {
		if (not Data() or (elem < Data()) or (elem >= Data() + m_info.capacity))
			return false;	// no data or element out of data
		if (bDiligent) {
			int32_t i = static_cast<int32_t>(reinterpret_cast<uint8_t*>(elem) - reinterpret_cast<uint8_t*>(Data()));
			if (i % sizeof(DATA_T))
				return false;	// elem in the data, but not properly aligned
		}
		return true;
	}

	// ----------------------------------------

	inline int32_t Index(DATA_T* elem) {
		return int32_t(elem - Data()) + m_info.offset;
	}

	// ----------------------------------------

	inline DATA_T* Pointer(int32_t i) {
		return Data() + (i - m_info.offset);
	}

	// ----------------------------------------

	void Destroy(void) {
		Base::Destroy();
		m_info.capacity = 0;
	}

	// ----------------------------------------

	DATA_T* Reserve(int32_t capacity, int32_t offset = 0) {
		if (m_info.capacity != capacity) {
			Destroy();
#if 0
			Base::Reserve(capacity);
#else
			DATA_T* data = new DATA_T[capacity];
			if (data) {
				Base::SetBuffer(data, false);
				m_info.capacity = capacity;
				Base::m_isStatic = false;
			}
#endif
			m_info.offset = offset;
		}
		return Data();
	}

	// ----------------------------------------

	inline DATA_T* Reserve(int32_t width, int32_t height, int32_t offset = 0) {
		Init(width, height);
		Reserve(width * height);
		return Data();
	}

	// ----------------------------------------

	inline DATA_T* Data() const {
#ifdef _DEBUG
		if (not (DATA_T*)(*this))
			return nullptr;
#endif
		return (DATA_T*)(*this);
	}

	// ----------------------------------------

	inline DATA_T* Data(int32_t i) const {
#ifdef _DEBUG
		if (not Data())
			return nullptr;
#endif
		return Data() + i - m_info.offset;
	}

	// ----------------------------------------

	void SetBuffer(DATA_T* data, int32_t capacity) {
		if (Data() != data) {
			Destroy();
			if (not data)
				Reset();
			else {
				Base::SetBuffer(data, true);
				m_info.capacity = capacity;
			}
		}
	}

	// ----------------------------------------

	inline DATA_T* Realloc(int32_t capacity, bool keepData) {
		DATA_T* p;
		try {
			p = new DATA_T[capacity];
		}
		catch (...) {
			return Data();
		}
		if (keepData and Data()) {
			memcpy(p, Data(), ((capacity > m_info.capacity) ? m_info.capacity : capacity) * sizeof(DATA_T));
			Clear(); // hack to avoid d'tors
		}
		Base::SetBuffer(p, false);
		return p;
	}


	DATA_T* Resize(int32_t capacity, bool keepData = true) {
		if (IsStatic())
			return Reserve(capacity);
		if (capacity > m_info.capacity) {
			Realloc(capacity, keepData);
			m_info.capacity = capacity;
			m_info.pos %= capacity;
		}
		return Data();
	}

	// ----------------------------------------

	inline const int32_t Capacity(void) const {
		return m_info.capacity;
	}

	// ----------------------------------------

	inline DATA_T* Current(void) {
		return Data(m_info.pos);
	}

	// ----------------------------------------

	inline int32_t Size(void) {
		return m_info.capacity * sizeof(DATA_T);
	}

	// ----------------------------------------

	inline bool IsValidIndex(int32_t i) {
		return (i >= 0) and (i < m_info.capacity);
	}

	// ----------------------------------------

	inline bool IsValidIndex(int32_t x, int32_t y) {
		return (x >= 0) and (x < m_info.width) and (y >= 0) and (y < m_info.height);
	}

	// ----------------------------------------

	inline int GetCheckedIndex(int32_t x, int32_t y) {
		return IsValidIndex(x, y) ? -1 : int(y * m_info.width + x);
	}

	// ----------------------------------------

	inline DATA_T& operator[] (const int32_t i) {
		return *Data(i - m_info.offset);
	}

	// ----------------------------------------

	inline DATA_T* operator()(int32_t x, int32_t y) {
		return Data(y * m_info.width + x);
	}

	// ----------------------------------------

	inline DATA_T* operator()(int32_t x, int32_t y, bool rangeCheck) { // always checks range; parameter only to distinguish from other operator()
		int i = GetCheckedIndex(x, y);
		return (i < 0) ? nullptr : Data(i);
	}

	// ----------------------------------------

	inline bool IsNull(DATA_T& v) {
		return (v == m_none);
	}

	// ----------------------------------------

	const DATA_T& operator()(int32_t x, int32_t y) const {
		return *Data(y * m_info.width + x);
	}

	// ----------------------------------------

	inline DATA_T* DataRow(int32_t y) {
		return Data(y * m_info.width);
	}

	// ----------------------------------------
	/*
	inline DATA_T* operator[] (const DATA_T v) {
		int32_t i = BinSearch(v);
		return (i < 0) ? nullptr : &m_handle[i];
	}
	*/
	// ----------------------------------------

	inline DATA_T* operator* () const {
		return Data();
	}

	// ----------------------------------------

	inline ManagedArray<DATA_T>& operator= (ManagedArray<DATA_T> const& source) {
		return CopyData(source.Data(), source.Capacity());
	}

	// ----------------------------------------

	inline ManagedArray<DATA_T>& operator= (ManagedArray<DATA_T>&& source) noexcept {
		return Move(source);
	}

	// ----------------------------------------

	inline ManagedArray<DATA_T>& operator= (std::initializer_list<DATA_T> data) {
		Reserve(int32_t(data.size()));
		Init();
		memcpy(Data(), data.begin(), sizeof(DATA_T) * data.size());
		return *this;
	}

	// ----------------------------------------

	inline DATA_T& operator= (DATA_T* source) {
		if (this != &source)
			memcpy(Data(), source, m_info.capacity * sizeof(DATA_T));
		return *Data();
	}

	// ----------------------------------------

	ManagedArray& CopyData(const ManagedArray& source, bool allowStatic = true, int32_t offset = 0) {
		if ((this != &source) and source.Data()) {
			if (allowStatic and source.IsStatic()) {
				Base::m_isStatic = true;
				BufferHandle() = source.BufferHandle();
			}
			else
				CopyData(source.Data(), source.Capacity(), offset);
		}
		return *this;
	}

	// ----------------------------------------

	ManagedArray& CopyData(DATA_T const* sourceData, int32_t count, int32_t offset = 0) {
		if (Resize(count + offset, false))
			memcpy(Data(offset), sourceData, std::min(m_info.capacity - offset, count) * sizeof(DATA_T));
		return *this;
	}

	// ----------------------------------------

	ManagedArray& Move(ManagedArray& source) {
		Destroy();
		memcpy(&m_info, &source.m_info, sizeof(ArrayInfo));
		BufferHandle() = std::move(source.BufferHandle());
		source.BufferHandle() = nullptr;
		source.Reset();
		return *this;
	}

	// ----------------------------------------

	inline DATA_T operator+ (ManagedArray<DATA_T>& source) {
		ManagedArray<DATA_T> a(*this);
		a += source;
		return a;
	}

	// ----------------------------------------

	inline DATA_T& operator+= (ManagedArray<DATA_T>& source) {
		int32_t offset = m_info.capacity;
		if (BufferHandle())
			Resize(m_info.capacity + source.m_info.capacity);
		return CopyData(source, offset);
	}

	// ----------------------------------------

	inline bool operator== (ManagedArray<DATA_T>& other) {
		return (m_info.capacity == other.m_info.capacity) and not (m_info.capacity and memcmp(Data(), other.Data()));
	}

	// ----------------------------------------

	inline bool operator!= (ManagedArray<DATA_T>& other) {
		return (m_info.capacity != other.m_info.capacity) or (m_info.capacity and memcmp(Data(), other.Data()));
	}

	// ----------------------------------------

	inline DATA_T* Start(void) { return Data(); }

	// ----------------------------------------

	inline DATA_T* End(void) { return (Data() and m_info.capacity) ? Data() + m_info.capacity - 1 : nullptr; }

	// ----------------------------------------

	inline DATA_T* operator++ (void) {
		if (not Data())
			return nullptr;
		if (m_info.pos < m_info.capacity - 1)
			m_info.pos++;
		else if (m_info.wrap)
			m_info.pos = 0;
		else
			return nullptr;
		return Data() + m_info.pos;
	}

	// ----------------------------------------

	inline DATA_T* operator-- (void) {
		if (not Data())
			return nullptr;
		if (m_info.pos > 0)
			m_info.pos--;
		else if (m_info.wrap)
			m_info.pos = m_info.capacity - 1;
		else
			return nullptr;
		return Data() + m_info.pos;
	}

	// ----------------------------------------

	inline DATA_T* operator+ (const int32_t i) {
		return Data() ? Data(i - m_info.offset) : nullptr;
	}

	// ----------------------------------------

	inline DATA_T* operator- (const int32_t i) { return Data() ? Data() - (i - m_info.offset) : nullptr; }

	// ----------------------------------------

	inline bool operator! () { return Data() == nullptr; }

	// ----------------------------------------

	inline int32_t Pos(void) { return m_info.pos; }

	// ----------------------------------------

	inline void Pos(int32_t pos) { m_info.pos = pos % m_info.capacity; }

	// ----------------------------------------

	inline void SetOffset(int32_t offset) { m_info.offset = offset; }

	// ----------------------------------------

	inline int32_t GetOffset(void) { return m_info.offset; }

	// ----------------------------------------
#if 0
	int32_t Read(CFile& cf, int32_t nCount = 0, int32_t nOffset = 0, bool bCompressed = 0) {
		if (not Data())
			return -1;
		if (nOffset >= m_info.capacity)
			return -1;
		if (not nCount)
			nCount = m_info.capacity - nOffset;
		else if (nCount > m_info.capacity - nOffset)
			nCount = m_info.capacity - nOffset;
		return cf.Read(DataPtr() + nOffset, sizeof(DATA_T), nCount, bCompressed);
	}

	// ----------------------------------------

	int32_t Write(CFile& cf, int32_t nCount = 0, int32_t nOffset = 0, int32_t bCompressed = 0) {
		if (not Data())
			return -1;
		if (nOffset >= m_info.capacity)
			return -1;
		if (not nCount)
			nCount = m_info.capacity - nOffset;
		else if (nCount > m_info.capacity - nOffset)
			nCount = m_info.capacity - nOffset;
		return cf.Write(DataPtr() + nOffset, sizeof(DATA_T), nCount, bCompressed);
	}
#endif

	// ----------------------------------------

	inline void SetWrap(bool wrap) { m_info.wrap = wrap; }

	// ----------------------------------------

	inline void SortAscending(int32_t left = 0, int32_t right = 0) {
		if (Data())
			QuickSort<DATA_T>::SortAscending(Data(), left, (right = 0) ? right : m_info.capacity - 1);
	}

	// ----------------------------------------

	inline void SortDescending(int32_t left = 0, int32_t right = 0) {
		if (Data())
			QuickSort<DATA_T>::SortDescending(Data(), left, (right > 0) ? right : m_info.capacity - 1);
	}

	// ----------------------------------------

	inline void SortAscending(QuickSort<DATA_T>::tComparator compare, int32_t left = 0, int32_t right = 0) {
		if (Data())
			QuickSort<DATA_T>::SortAscending(Data(), left, (right > 0) ? right : m_info.capacity - 1, compare);
	}

	// ----------------------------------------

	inline void SortDescending(QuickSort<DATA_T>::tComparator compare, int32_t left = 0, int32_t right = 0) {
		if (Data())
			QuickSort<DATA_T>::SortDescending(Data(), left, (right > 0) ? right : m_info.capacity - 1, compare);
	}

	// ----------------------------------------

	template<typename KEY_T>
	inline int32_t Find(KEY_T const& key, int(__cdecl* compare) (DATA_T const&, KEY_T const&), int32_t left = 0, int32_t right = 0) {
		return Data() ? this->BinSearch(Data(), key, compare, left, (right > 0) ? right : m_info.capacity - 1) : -1;
	}
};

// =================================================================================================

inline int32_t operator- (char* v, ManagedArray<char>& a) { return a.Index(v); }
inline int32_t operator- (uint8_t* v, ManagedArray<uint8_t>& a) { return a.Index(v); }
inline int32_t operator- (int16_t* v, ManagedArray<int16_t>& a) { return a.Index(v); }
inline int32_t operator- (uint16_t* v, ManagedArray<uint16_t>& a) { return a.Index(v); }
inline int32_t operator- (uint32_t* v, ManagedArray<uint32_t>& a) { return a.Index(v); }
inline int32_t operator- (int32_t* v, ManagedArray<int32_t>& a) { return a.Index(v); }

// =================================================================================================

class CharArray : public ManagedArray<char> {
public:
	inline char* operator= (const char* source) {
		int32_t l = int32_t(strlen(source) + 1);
		if ((l > this->m_info.capacity) and not this->Resize(this->m_info.capacity + l))
			return nullptr;
		memcpy(this->Data(), source, l);
		return this->Data();
	}
};

// =================================================================================================

template<typename DATA_T>
using SharedArray = ManagedArray<DATA_T, SharedPointer<DATA_T>>;

class ByteArray : public ManagedArray<uint8_t> {
public:
	ByteArray(const int32_t nLength) {
		Reserve(nLength);
		Init();
	}
};

class ShortArray : public ManagedArray<int16_t> {
public:
	ShortArray(const int32_t nLength) {
		Reserve(nLength);
		Init();
	}
};

class UShortArray : public ManagedArray<uint16_t> {
public:
	UShortArray(const int32_t nLength) {
		Reserve(nLength);
		Init();
	}
};

class IntArray : public ManagedArray<int32_t> {
public:
	IntArray(const int32_t nLength) {
		Reserve(nLength);
		Init();
	}
};

class UIntArray : public ManagedArray<int32_t> {
public:
	UIntArray(const int32_t nLength) {
		Reserve(nLength);
		Init();
	}
};

class SizeArray : public ManagedArray<size_t> {
public:
	SizeArray(const int32_t nLength) {
		Reserve(nLength);
		Init();
	}
};

class FloatArray : public ManagedArray<float> {
public:
	FloatArray(const int32_t nLength) {
		Reserve(nLength);
		Init();
	}
};

// =================================================================================================

template < class DATA_T, int32_t capacity >
class StaticArray : public ManagedArray < DATA_T > {

protected:
	DATA_T		m_buffer[capacity];

public:
	StaticArray() { Reserve(capacity); }

	DATA_T* Reserve(int32_t capacity) {
		this->SetBuffer(m_buffer, capacity);
		return m_buffer;
	}
	void Destroy(void) {}
};

// =================================================================================================

template < typename DATA_T >
class Array2D : public ManagedArray < DATA_T > {
public:
	int32_t	m_rows;
	int32_t	m_cols;

	DATA_T& operator()(int32_t x, int32_t y) {
		return this->Data(x * m_cols + y);
	}

	const DATA_T& operator()(int32_t x, int32_t y) const {
		return this->Data(x * m_cols + y);
	}

	DATA_T* GetRow(int32_t y) {
		return this->Data(y * m_cols);
	}
};

// =================================================================================================
