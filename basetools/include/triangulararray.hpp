#pragma once

#include "array.hpp"
#include <utility>

// =================================================================================================

template<typename DATA_T>
class TriangularArray {
public:
	int32_t					m_width;
	ManagedArray<DATA_T>	m_data;
	TriangularArray() 
		: m_width(0)
	{ }

	inline void Create(int32_t width) {
		m_width = width;
		m_data.Resize(Index(width, width));
	}

	inline void Destroy() {
		m_data.Reset();
		m_width = 0;
	}

	inline uint32_t Index(uint32_t x, uint32_t y) const noexcept {
		return (y > x) ? (y * (y + 1)) / 2 + x : (x * (x + 1)) / 2 + y;
	}

	inline auto operator()(uint32_t x, uint32_t y) -> decltype(std::declval<ManagedArray<DATA_T>&>()[0]) {
		uint32_t i = Index(x, y);
		return m_data[i];
	}

	inline auto operator()(uint32_t x, uint32_t y) const -> decltype(std::declval<const ManagedArray<DATA_T>&>()[0]) {
		uint32_t i = Index(x, y);
		return m_data[i];
	}
};

// =================================================================================================
