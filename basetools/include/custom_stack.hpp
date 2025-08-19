// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <stdexcept>
#include <utility>
#include "custom_array.hpp"

//-----------------------------------------------------------------------------

template < class DATA_T > 
class Stack : public Array< DATA_T > {
	protected:
		int32_t	m_tos;
		int32_t	m_growth;	// how much to increase buffer size if the stack is full and something gets pushed on it; -1 = no growth, zero = double capacity every time growth is needed
		bool	m_reorder;	// allow reordering on element deletion

	public:
		Stack () { Init (); }

		Stack (int32_t capacity, int32_t growth = 0, bool reorder = false) { 
			Init (); 
			Reserve (capacity, growth, reorder);
			}


		~Stack() { Destroy (); }


		inline void Reset (void) { 
			m_tos = 0; 
		}


		inline void Clear(DATA_T clearValue) {
			for (int32_t i = 0; i < m_tos; i++)
				(*this)[i] = clearValue;
			Reset();
		}


		inline bool AllowReordering(bool reorder) {
			m_reorder = reorder;
		}


		inline void Init (void) { 
			m_growth = 0;
			Reset();
			Array<DATA_T>::Init ();
			}


		inline bool Grow (const int32_t i = 1) {
			if (m_tos + i >= this->m_info.capacity) {
				if (not m_growth) {
					throw std::runtime_error("stack out of space");
					return false;
				}
				if (not this->Resize(this->m_info.capacity + m_growth)) {
					throw std::runtime_error("stack expansion failed");
					return false;
				}
				//#pragma omp critical
			}
			m_tos += i;
			return true;
			}

		
		template<typename T>
		inline bool Push (T&& data) { 
			if (not Grow ())
				return false;
//#pragma omp critical
			*(this->Data(m_tos - 1)) = std::forward<T>(data);
			return true;
			}
	
		
		inline int32_t Shrink (int32_t i = 1) {
//#pragma omp critical
			if (i >= m_tos)
				m_tos = 0;
			else
				m_tos -= i;
			return m_tos;
			}


		inline DATA_T& Pop (void) {
//#pragma omp critical
			return *(this->Data(Shrink()));
			}


		inline void Truncate (int32_t i = 1) {
			if (i < m_tos)
				m_tos = i;
			}


		inline int32_t Find (DATA_T& data) {
			for (int32_t i = 0; i < m_tos; i++)
				if (this->m_data [i] == data)
					return i;
			return m_tos;
			}


		inline int32_t ToS (void) { return m_tos; }


		inline DATA_T* Top (void) { return (this->m_data and m_tos) ? this->m_data + m_tos - 1 : NULL; }


		inline bool Delete (int32_t i) {
			if (i >= m_tos) 
				return false;
//#pragma omp critical
			if (i < --m_tos) 
				memcpy(this->m_data + i, this->m_data + i + 1, sizeof(DATA_T) * (m_tos - i));
			return true;
			}


		inline bool DeleteElement (DATA_T& data) { return Delete (Find (data));	}


		inline DATA_T& Pull (DATA_T& data, int32_t i) {
//#pragma omp critical
			if (i < m_tos) {
				data = this->m_data [i];
				Delete (i);
				}
			return data;
			}


		inline DATA_T Pull (int32_t i) {
			DATA_T	v;
			return Pull (v, i);
			}


		inline DATA_T* GetRef(int32_t i) {
			//#pragma omp critical
			return (i < m_tos) ? this->m_data + i : nullptr;
		}


		inline void Destroy (void) {
			Array<DATA_T>::Destroy ();
			m_tos = 0;
			}


		inline DATA_T *Reserve (int32_t capacity, int32_t growth = 0, bool reorder = false) {
			Destroy ();
			m_growth = (static_cast<ptrdiff_t>(growth) < 0) ? 0 : (growth > 0) ? growth : capacity;
			m_reorder = reorder;
			return Array<DATA_T>::Reserve (capacity);
			}


		inline int32_t Growth (void) { return m_growth; }


		inline void SetGrowth (int32_t growth) { m_growth = growth; }


		inline void SortAscending (int32_t left = 0, int32_t right = 0) { 
			if (this->m_data)
				QuickSort<DATA_T>::SortAscending (this->m_data, left, (right >= 0) ? right : m_tos - 1); 
				}


		inline void SortDescending (int32_t left = 0, int32_t right = 0) {
			if (this->m_data)
				QuickSort<DATA_T>::SortDescending (this->m_data, left, (right >= 0) ? right : m_tos - 1);
			}


		inline void SortAscending (QuickSort<DATA_T>::tComparator compare, int32_t left = 0, int32_t right = 0) {
			if (this->m_data)
				QuickSort<DATA_T>::SortAscending (this->m_data, left, (right >= 0) ? right : m_tos - 1, compare);
			}


		inline void SortDescending (QuickSort<DATA_T>::tComparator compare, int32_t left = 0, int32_t right = 0) {
			if (this->m_data)
				QuickSort<DATA_T>::SortDescending (this->m_data, left, (right >= 0) ? right : m_tos - 1, compare);
			}


		inline int32_t BinSearch (DATA_T key, int32_t left = 0, int32_t right = 0) {
			return this->m_data ? QuickSort<DATA_T>::BinSearch (this->m_data, left, (right >= 0) ? right : m_tos - 1, key) : -1;
			}

	};

//-----------------------------------------------------------------------------
