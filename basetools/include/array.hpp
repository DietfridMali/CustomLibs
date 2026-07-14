// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#ifndef NOMINMAX
#	define NOMINMAX
#endif

#if (USE_STD || USE_STD_VECTOR)

#	include <span>
#	include <vector>
#	include <algorithm>
#	include <utility>
#	include "std_array.hpp"

#else

#	include "custom_array.hpp"

#endif //USE_STD_VECTOR

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <algorithm>
#include <initializer_list>

// Fixed-capacity array after AutoArray's model: std::array as a member (composition,
// no inheritance from std types). Length is set once (Create) and stays fixed.
// Provides the AutoArray-style (capitalized) interface AND the std::array-style
// (lowercase + aggregate-like init) interface so rendertools/Smiley-Battle keep working.
template <typename DATA_T, size_t CAPACITY>
class StaticArray {
private:
    std::array<DATA_T, CAPACITY> m_array{};
    int32_t                      m_length{ static_cast<int32_t>(CAPACITY) };

public:
    using value_type = DATA_T;

    StaticArray() = default;

    StaticArray(std::initializer_list<DATA_T> data) {
        m_length = static_cast<int32_t>((data.size() < CAPACITY) ? data.size() : CAPACITY);
        std::copy_n(data.begin(), static_cast<size_t>(m_length), m_array.begin());
    }

    // ---- AutoArray-style interface (capitalized) ----
    inline int32_t Length(void) const noexcept { return m_length; }
    inline int32_t Capacity(void) const noexcept { return static_cast<int32_t>(CAPACITY); }
    inline int32_t Size(void) const noexcept { return m_length * static_cast<int32_t>(sizeof(DATA_T)); }
    inline bool IsEmpty(void) const noexcept { return m_length == 0; }

    inline DATA_T* Data(int32_t i = 0) noexcept { return m_array.data() + i; }
    inline const DATA_T* Data(int32_t i = 0) const noexcept { return m_array.data() + i; }
    inline DATA_T* DataPtr(int32_t i = 0) noexcept { return m_array.data() + i; }
    inline const DATA_T* DataPtr(int32_t i = 0) const noexcept { return m_array.data() + i; }
    inline DATA_T* Buffer(int32_t i = 0) noexcept { return m_array.data() + i; }
    inline const DATA_T* Buffer(int32_t i = 0) const noexcept { return m_array.data() + i; }

    inline DATA_T* operator+(int32_t i) noexcept { return m_array.data() + i; }
    inline const DATA_T* operator+(int32_t i) const noexcept { return m_array.data() + i; }

    inline DATA_T* Create(int32_t length = static_cast<int32_t>(CAPACITY), const char* = nullptr) noexcept {
        m_length = (length < static_cast<int32_t>(CAPACITY)) ? length : static_cast<int32_t>(CAPACITY);
        return m_array.data();
    }
    inline void Destroy(void) noexcept { m_length = 0; }

    inline void Clear(uint8_t filler = 0) noexcept {
        if constexpr (std::is_trivial_v<DATA_T>)
            memset(m_array.data(), filler, CAPACITY * sizeof(DATA_T));
        else
            m_array.fill(DATA_T{});
    }
    inline void Fill(const DATA_T& value) noexcept { m_array.fill(value); }

    // ---- std::array-style interface (rendertools / Smiley-Battle) ----
    inline DATA_T& operator[](size_t i) noexcept { return m_array[i]; }
    inline const DATA_T& operator[](size_t i) const noexcept { return m_array[i]; }
    inline size_t size(void) const noexcept { return CAPACITY; }
    inline DATA_T* data(void) noexcept { return m_array.data(); }
    inline const DATA_T* data(void) const noexcept { return m_array.data(); }
    inline DATA_T& front(void) noexcept { return m_array.front(); }
    inline DATA_T& back(void) noexcept { return m_array.back(); }
    inline auto begin(void) noexcept { return m_array.begin(); }
    inline auto end(void) noexcept { return m_array.end(); }
    inline auto begin(void) const noexcept { return m_array.begin(); }
    inline auto end(void) const noexcept { return m_array.end(); }
};
