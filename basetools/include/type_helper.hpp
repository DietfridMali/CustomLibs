#pragma once

#include <type_traits>

// Prüft, ob ein gegebener Ausdruck als Member-Zeiger existiert
template <auto Member>
constexpr bool is_static_member_v = !std::is_member_object_pointer_v<decltype(Member)>;

template<typename T>
void InitializeAnyType(T& v) {
    if constexpr (std::is_array_v<T>) {
        for (size_t i = 0; i < std::extent_v<T>; ++i)
            InitializeAnyType(v[i]);
    }
    else if constexpr (std::is_default_constructible_v<T>) {
        v = T{};
    }
    else {
        static_assert(std::is_default_constructible_v<T>,
            "Type must be default constructible");
    }
}