#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <filesystem>

// =================================================================================================

template<typename DATA_T>
class AutoArray {
private:
    std::vector<DATA_T> m_array;
    int32_t             m_width{ 0 };
    int32_t             m_height{ 0 };
    bool                m_autoFit{ false };
    bool                m_isShrinkable{ true };
    DATA_T              m_defaultValue{};

public:
	static const int32_t MaxIndex = std::numeric_limits<int32_t>::max();

    // Konstruktor für 1D-AutoArray
    inline AutoArray(int32_t size = 0)
        : m_array(static_cast<size_t>(std::max<int32_t>(size, 0))) {
    }

    inline AutoArray(std::initializer_list<DATA_T> data)
        : m_array(data)
    {
    }

    // Konstruktor für 2D-AutoArray
    inline AutoArray(int32_t width, int32_t height)
        : m_array(ValidatedSize2D(width, height, 0))
        {
        assert(width * height > 0 and "Width and height must be > 0");
#if defined(_DEBUG)
        if (width * height <= 0)
            throw std::invalid_argument("AutoArray: invalid width or height arguments (must both be > 0)");
        m_width = (Length() > 0) and (ValidatedSize(width, 0) > -1) ? width : 0;
        m_height = (Length() > 0) and (ValidatedSize(height, 0) > -1) ? height : 0;
        Resize(width * height);
#endif    
    }

    AutoArray(const AutoArray& other)
        : m_array(other.m_array),
        m_width(other.m_width),
        m_height(other.m_height),
        m_autoFit(other.m_autoFit),
        m_isShrinkable(other.m_isShrinkable),
        m_defaultValue(other.m_defaultValue)
    {
    }

    // Move-Konstruktor
    AutoArray(AutoArray&& other) noexcept
        : m_array(std::move(other.m_array)),
        m_width(std::exchange(other.m_width, 0)),
        m_height(std::exchange(other.m_height, 0)),
        m_autoFit(std::exchange(other.m_autoFit, false)),
        m_isShrinkable(std::exchange(other.m_isShrinkable, true)),
        m_defaultValue(std::move(other.m_defaultValue))
    {
    }

    // Copy-Zuweisungsoperator
    AutoArray& operator=(const AutoArray& other) {
        if (this != &other) {
            m_array = other.m_array;
            m_width = other.m_width;
            m_height = other.m_height;
            m_autoFit = other.m_autoFit;
            m_isShrinkable = other.m_isShrinkable;
            m_defaultValue = other.m_defaultValue;
        }
        return *this;
    }

    // Move-Zuweisungsoperator
    AutoArray& operator=(AutoArray&& other) noexcept {
        if (this != &other) {
            m_array = std::move(other.m_array);
            m_width = std::exchange(other.m_width, 0);
            m_height = std::exchange(other.m_height, 0);
            m_autoFit = std::exchange(other.m_autoFit, false);
            m_isShrinkable = std::exchange(other.m_isShrinkable, true);
            m_defaultValue = std::move(other.m_defaultValue);
        }
        return *this;
    }

    AutoArray& operator=(std::initializer_list<DATA_T> data) {
        m_array = data;
        return *this;
    }

    inline int32_t Capacity(void) const noexcept { return static_cast<int32_t>(m_array.capacity()); }

    // Zugriff auf Länge
    inline int32_t Length(void) const noexcept { return static_cast<int32_t>(m_array.size()); }

    inline bool IsEmpty(void) const noexcept { return Length() == 0; }

    // Gesamte Datenmenge in Bytes
    inline int32_t DataSize() const noexcept { return Length() * static_cast<int32_t>(sizeof(DATA_T)); }

    inline int32_t AutoFit(int32_t i) {
        if (not m_autoFit)
			return (i < Length()) ? i : -1;
        if (ValidatedSize(static_cast<size_t>(i)) < 0)
            return -1;
        if (i >= Length())
            m_array.resize(static_cast<size_t>(i) + 1, m_defaultValue);
        return i;
    }

    // 1D-Indexzugriff
    inline decltype(auto) operator[](int32_t i) {
		if (AutoFit(i) < 0)
            throw std::out_of_range("AutoArray::operator[]: index out of range");
#if defined(_DEBUG)
        return m_array.at(static_cast<size_t>(i));
#else
        return m_array[static_cast<size_t>(i)];
#endif
    }

    inline decltype(auto) operator[](int32_t i) const {
#if defined(_DEBUG)
        return m_array.at(static_cast<size_t>(i));
#else
        return m_array[static_cast<size_t>(i)];
#endif
    }

    // 2D-Zugriff (x, y)
    inline DATA_T& operator()(int32_t x, int32_t y) {
        assert(m_width > 0 and m_height > 0);
#if defined(_DEBUG)
        int32_t i = AutoFit(ValidatedSize(static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)));
        if (i < 0)
            throw std::out_of_range("AutoArray::operator(): indices out of range");
        return m_array.at(static_cast<size_t>(i));
#else
        return m_array[static_cast<size_t>(y * m_width + x)];
#endif
    }

    inline int32_t ValidatedSize(size_t size, int32_t defaultSize = -1) const noexcept {
        return (size > static_cast<size_t>(MaxIndex)) ? defaultSize : int32_t(size);
    }

    inline int32_t ValidatedSize2D(int32_t x, int32_t y, int32_t defaultSize = -1) const noexcept {
		return ((x < 0) or (y < 0)) ? defaultSize : ValidatedSize(static_cast<size_t>(x) * static_cast<size_t>(y), defaultSize);
    }

    inline bool IsValidIndex(int32_t i) const noexcept { 
        return (i >= 0) and (m_autoFit or (i < Length())); 
    }

    inline bool IsValidIndex(int32_t x, int32_t y) const noexcept { 
        return m_autoFit ? ValidatedSize2D(x, y) > -1 : (x >= 0) and (y >= 0) and (x < m_width) and (y < m_height);
    }

    inline int32_t GetCheckedIndex(int32_t x, int32_t y) const noexcept { 
        return IsValidIndex(x, y) ? int32_t(y * m_width + x) : -1; 
    }

    inline int32_t GetIndex(int32_t x, int32_t y) const noexcept { 
        return int32_t(y * m_width + x); 
    }

    inline DATA_T* operator()(int32_t x, int32_t y, bool rangeCheck) {
        int32_t i = AutoFit(rangeCheck ? GetCheckedIndex(x, y) : GetIndex(x, y));
        return (i < 0) ? nullptr : Data(i);
    }

    inline const DATA_T& operator()(int32_t x, int32_t y) const {
        assert(m_width > 0 and m_height > 0);
#if defined(_DEBUG)
        return m_array.at(static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x));
#else
        return m_array[static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x)];
#endif
    }

    void Append(const DATA_T& data) { 
        if (Length() < MaxIndex)
            m_array.push_back(data);
    }

    AutoArray<DATA_T>& Append(AutoArray<DATA_T>& other, bool copyData) {
        int32_t size = ValidatedSize(Length() + other.Length());
		if (size >= 0) {
            Reserve(size);
            if (copyData)
                m_array.insert(m_array.end(), other.begin(), other.end());
            else {
                m_array.insert(m_array.end(), std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()));
                other.Clear();
            }
        }
        return *this;
    }

    AutoArray& operator+=(const AutoArray& other) {
        if (std::addressof(other) == this) 
            return *this;
        return Append(const_cast<AutoArray&>(other), true);
    }

    AutoArray& operator+=(AutoArray&& other) {
        if (std::addressof(other) == this) 
            return *this;
        return Append(other, false);
        return *this;
    }


    AutoArray operator+(const AutoArray& other) const {
        AutoArray result;
        int32_t size = ValidatedSize(Length() + other.Length());
        if (size >= 0) {
            result.m_array.reserve(static_cast<size_t>(size));
            result.m_array.insert(result.m_array.end(), m_array.begin(), m_array.end());
            result.m_array.insert(result.m_array.end(), other.m_array.begin(), other.m_array.end());
        }
        return result;
    }


    void Fill(DATA_T value) noexcept {
        std::fill(m_array.begin(), m_array.end(), value);
    }


    DATA_T* Append(void) {
		if (Length() == MaxIndex)
            return nullptr;
        m_array.emplace_back();
        return &m_array.back();
    }


    bool Push(DATA_T data) { 
        if (Length() < MaxIndex) {
            m_array.push_back(data);
            return true;
        }
        return false;
    }


    DATA_T Pop(void) {
        if (m_array.empty())
            return DATA_T();
        DATA_T data = m_array.back();
        m_array.pop_back();
        return data;
    }

    
    template<typename... Args>
    DATA_T* Append(Args&&... args) {
        auto argCount = sizeof...(Args);
		if (size_t(Length()) + argCount > static_cast<size_t>(MaxIndex))
            return nullptr;
        m_array.emplace_back(std::forward<Args>(args)...);
        return &m_array.back();
    }
#if 0
    // Zeiger auf Rohdaten (z.B. für OpenGL)
    inline DATA_T* Data(int32_t i = 0) noexcept { 
        return m_array.data() + i; 
    }

    inline const DATA_T* Data(int32_t i = 0) const noexcept { 
        return m_array.data() + i; 
    }
#else
    inline auto Data(int32_t i = 0) noexcept { 
        return m_array.data() + i; 
    }

    inline auto Data(int32_t i = 0) const noexcept { 
        return m_array.data() + i; 
    }
#endif
    DATA_T* DataRow(int32_t y) {
#if defined(_DEBUG)
        if (m_width * m_height <= 0)
            throw std::invalid_argument("AutoArray: invalid width or height arguments (must both be > 0)");
#endif    
        return Data(y * m_width);
    }

    inline void Reserve(int32_t capacity) {
        if (ValidatedSize(capacity) > -1)
            m_array.reserve(static_cast<size_t>(capacity));
    }

    inline bool AllowResize(size_t newSize) const noexcept { 
        return (ValidatedSize(newSize) > -1) and (m_isShrinkable or (newSize > static_cast<size_t>(Length())));
    }

    // Resize-Methoden
    inline DATA_T* Resize(int32_t newSize) {
        if (AllowResize(static_cast<size_t>(newSize)))
            m_array.resize(static_cast<size_t>(newSize));
        return Data();
    }

    inline DATA_T* Resize(int32_t newSize, const DATA_T& value) {
        if (AllowResize(static_cast<size_t>(newSize)))
            m_array.resize(static_cast<size_t>(newSize), value);
        return Data();
    }

    inline DATA_T* Resize(int32_t width, int32_t height) {
		if (ValidatedSize2D(width, height) > -1) {
            m_array.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
            m_width = width;
            m_height = height;
        }
        return Data();
    }

    inline void Clear(void) {
        m_array.clear();
    }

    inline void Reset(void) {
        m_array.clear();
        m_array.shrink_to_fit();
    }

    inline auto begin() noexcept { return m_array.begin(); }

    inline auto end() noexcept { return m_array.end(); }

    inline auto begin() const noexcept { return m_array.begin(); }

    inline auto end() const noexcept { return m_array.end(); }

    inline auto rbegin() noexcept { return m_array.rbegin(); }

    inline auto rend() noexcept { return m_array.rend(); }

    inline auto rbegin() const noexcept { return m_array.rbegin(); }

    inline auto rend() const noexcept { return m_array.rend(); }

    // Typecast-Operator zu std::vector<DATA_T>
    inline operator std::vector<DATA_T>& () noexcept { return m_array; }

    inline operator const std::vector<DATA_T>& () const noexcept { return m_array; }

    template <typename Predicate>
    auto Find(Predicate compare) {
        return std::find_if(m_array.begin(), m_array.end(), compare);
    }


    template<typename KEY_T, typename COMPARE_T>
    int32_t FindLinear(const KEY_T& key, COMPARE_T compare) const {
        int32_t i = 0;
        for (const auto& data : m_array) {
            if (not compare(data, key))
                return i;
            ++i;
        }
        return -1;
    }


    template<typename KEY_T, typename COMPARE_T>
    int32_t FindBinary(const KEY_T& key, COMPARE_T compare) const {
        auto it = std::lower_bound(
            m_array.begin(), m_array.end(), key,
            [&](const DATA_T& data, const KEY_T& key) {
                return compare(data, key) < 0; // a < b
            }
        );
        if (it != m_array.end() and compare(*it, key) == 0)
            return static_cast<int32_t>(std::distance(m_array.begin(), it));
        else
            return -1;
    }

    inline bool GetAutoFit(void) const noexcept {
        return m_autoFit;
    }

    inline bool SetAutoFit(bool newSetting) noexcept {
        bool currentSetting = m_autoFit;
        m_autoFit = newSetting;
        return currentSetting;
    }

    inline bool GetShrinkable(void) const noexcept {
        return m_isShrinkable;
    }

    inline bool SetShrinkable(bool newSetting) noexcept {
        bool currentSetting = m_isShrinkable;
        m_isShrinkable = newSetting;
        return currentSetting;
    }

    inline void SetDefaultValue(const DATA_T& defaultValue) {
        m_defaultValue = defaultValue;
    }


    bool LoadFromFile(const std::string & filename, uint32_t elemCount = 0) {
        if (filename.empty())
            return false;
        std::ifstream f(filename.c_str(), std::ios::binary);
        if (not f)
            return false;

        f.seekg(0, std::ios::end);
        std::streamoff fileSize = f.tellg();
        f.seekg(0, std::ios::beg);
        if (elemCount == 0)
			elemCount = uint32_t(fileSize) / sizeof(DATA_T);
        size_t dataSize = size_t(elemCount) * sizeof(DATA_T);
        if (fileSize != std::streamoff(dataSize))
            return false;

        if (Length() != elemCount)
            Resize(int32_t(elemCount));

        f.read(reinterpret_cast<char*>(Data()), dataSize);
        return f.good();
    }


    bool SaveToFile(const std::string& filename) const {
        if (filename.empty())
            return false;
        std::ofstream f(filename.c_str(), std::ios::binary | std::ios::trunc);
        if (not f)
            return false;
        f.write(reinterpret_cast<const char*>(Data()), size_t(Length()) * sizeof(DATA_T));
        return f.good();
    }

};

// =================================================================================================

class ByteArray : public AutoArray<uint8_t> {
public:
    ByteArray(const int32_t nLength) {
        Resize(nLength);
    }
};

class ShortArray : public AutoArray<int16_t> {
public:
    ShortArray(const int32_t nLength) {
        Resize(nLength);
    }
};

class UShortArray : public AutoArray<uint16_t> {
public:
    UShortArray(const int32_t nLength) {
        Resize(nLength);
    }
};

class IntArray : public AutoArray<int32_t> {
public:
    IntArray(const int32_t nLength) {
        Resize(nLength);
    }
};

class UIntArray : public AutoArray<uint32_t> {
public:
    UIntArray(const int32_t nLength) {
        Resize(nLength);
    }
};

class SizeArray : public AutoArray<size_t> {
public:
    SizeArray(const int32_t nLength) {
        Resize(nLength);
    }
};

class FloatArray : public AutoArray<float> {
public:
    FloatArray(const int32_t nLength) {
        Resize(nLength);
    }
};

// =================================================================================================
