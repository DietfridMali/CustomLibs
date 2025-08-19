#pragma once

#if (USE_STD || USE_STD_MAP)

#	include "std_map.hpp"

template <typename KEY_T, typename DATA_T>
using Dictionary = StdMap<KEY_T, DATA_T>;

#else

#	include "dictionary.hpp"

template <typename KEY_T, typename DATA_T>
using Dictionary = AVLTree<KEY_T, DATA_T>;

#endif
