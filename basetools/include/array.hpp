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

template <typename DATA_T, size_t size>
using StaticArray = std::array<DATA_T, size>;
