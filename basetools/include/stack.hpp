#pragma once

#include <cstdint>
#include <utility>
#include <algorithm>
#include <functional>

#include "array.hpp"

// =================================================================================================
// Stack on top of AutoArray.
//
// The fill level ("top of stack") is the AutoArray's size (Length()); the allocation is its
// capacity. This mirrors the legacy CStack API so a CStack member/variable can be replaced by a
// plain type-name swap (CStack -> Stack). Create() only reserves capacity, the stack stays empty;
// Push and Grow move the fill level, so Length() == ToS() at all times.

template <typename DATA_T>
class Stack : public AutoArray<DATA_T> {
    using Base = AutoArray<DATA_T>;

public:
    Stack() = default;

    explicit Stack(int32_t length) { Create(length); }

    // reserve capacity only - the stack stays empty (ToS() == 0)
    inline DATA_T* Create(int32_t length, const char* = nullptr) {
        Base::Reserve(length);
        return Base::DataPtr();
    }

    inline void Reset(void) { Base::Clear(); }      // size -> 0, capacity retained

    inline void Destroy(void) { Base::Clear(); }

    inline uint32_t ToS(void) const noexcept { return static_cast<uint32_t>(Base::Length()); }

    inline bool Push(const DATA_T& elem) {
        Base::Append(elem);
        return true;
    }

    inline DATA_T Pop(void) { return Base::Pop(); }

    inline DATA_T* Top(void) noexcept {
        int32_t n = Base::Length();
        return n ? Base::DataPtr(n - 1) : nullptr;
    }

    // grow the fill level by i (additive), allocating as needed
    inline bool Grow(uint32_t i = 1) {
        Base::Resize(Base::Length() + static_cast<int32_t>(i));
        return true;
    }

    inline void Shrink(uint32_t i = 1) {
        int32_t n = Base::Length();
        Base::Resize((static_cast<int32_t>(i) >= n) ? 0 : n - static_cast<int32_t>(i));
    }

    inline void Truncate(uint32_t i = 1) {
        if (static_cast<int32_t>(i) < Base::Length())
            Base::Resize(static_cast<int32_t>(i));
    }

    inline uint32_t Find(const DATA_T& elem) {
        int32_t n = Base::Length();
        for (int32_t i = 0; i < n; i++)
            if (Base::Data()[i] == elem)
                return static_cast<uint32_t>(i);
        return static_cast<uint32_t>(n);
    }

    inline bool Delete(uint32_t i) {
        int32_t n = Base::Length();
        if (static_cast<int32_t>(i) >= n)
            return false;
        for (int32_t j = static_cast<int32_t>(i); j < n - 1; j++)
            Base::DataPtr(j) = std::move(Base::DataPtr(j + 1));
        Base::Resize(n - 1);
        return true;
    }

    inline bool DeleteElement(const DATA_T& elem) { return Delete(Find(elem)); }

    inline DATA_T& Pull(DATA_T& elem, uint32_t i) {
        if (static_cast<int32_t>(i) < Base::Length()) {
            elem = Base::DataPtr(i);
            Delete(i);
        }
        return elem;
    }

    inline DATA_T Pull(uint32_t i) {
        DATA_T v {};
        return Pull(v, i);
    }

    // std::vector already grows amortized O(1) - growth control kept as no-op for source compatibility
    inline uint32_t Growth(void) const noexcept { return 0; }

    inline void SetGrowth(uint32_t) noexcept { }

    inline void SortAscending(int32_t left = 0, int32_t right = -1) {
        int32_t n = Base::Length();
        if (n > 0)
            std::sort(Base::DataPtr(left), Base::DataPtr((right >= 0) ? right + 1 : n));
    }

    inline void SortDescending(int32_t left = 0, int32_t right = -1) {
        int32_t n = Base::Length();
        if (n > 0)
            std::sort(Base::Data(left), Base::Data((right >= 0) ? right + 1 : n), std::greater<DATA_T>());
    }
};

// =================================================================================================
