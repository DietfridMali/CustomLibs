// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#ifndef _WIN32
#	define __cdecl
#endif

#include <stdexcept>

//-----------------------------------------------------------------------------

template < typename DATA_T > 
class QuickSort {
	public:
		typedef int (__cdecl *tComparator) (const DATA_T*, const DATA_T*);

		template<typename KEY_T>
		using tSearchComparator = int(__cdecl*)(const DATA_T&, const KEY_T&);

		// holds a copy of the median data for sorting; one global variable should work 
		// as it is only used during the sorting part and not during the recursive descent
		tComparator	m_compare;
		DATA_T		m_median;

//-----------------------------------------------------------------------------

inline void Swap(DATA_T* left, DATA_T* right)
{
	DATA_T h;
	memcpy (&h, left, sizeof (DATA_T));
	memcpy(left, right, sizeof(DATA_T));
	memcpy(right, &h, sizeof(DATA_T));
	memset(&h, 0, sizeof(DATA_T));
}

//-----------------------------------------------------------------------------

void SortAscending(DATA_T* data, int32_t left, int32_t right)
{
	int32_t	l = left,
			r = right;
	DATA_T*	m = new DATA_T(data [(l + r) / 2]);

	do {
		while (data[l] < *m)
			l++;
		while (data[r] > *m)
			r--;
		if (l <= r) {
			if (l < r)
				Swap(data + l, data + r);
			l++;
			r--;
		}
	} while (l <= r);
	memset(&m_median, 0, sizeof(DATA_T));

	if (l < right)
		SortAscending(data, l, right);
	if (left < r)
		SortAscending(data, left, r);
}

//-----------------------------------------------------------------------------

void SortDescending(DATA_T* data, int32_t left, int32_t right)
{
	int32_t	l = left,
			r = right;

	memcpy(&m_median, data + (l + r) / 2, sizeof(DATA_T));
	do {
		while (data[l] > m_median)
			l++;
		while (data[r] < m_median)
			r--;
		if (l <= r) {
			if (l < r)
				Swap(data + l, data + r);
			l++;
			r--;
		}
	} while (l <= r);
	memset(&m_median, 0, sizeof(DATA_T));

	if (l < right)
		SortDescending(data, l, right);
	if (left < r)
		SortDescending(data, left, r);
}

//-----------------------------------------------------------------------------

void Sort(DATA_T* data, int32_t left, int32_t right, int direction)
{
	int32_t	l = left,
			r = right;
	
	// flat copy to avoid any complicated copy operations; data only needed to have a fixed median value
	memcpy(&m_median, data + (l + r) / 2, sizeof(DATA_T)); 
	do {
		while (direction * m_compare(data + l, &m_median) > 0)
			l++;
		while (direction * m_compare(data + r, &m_median) < 0)
			r--;
		if (l <= r) {
			if (l < r)
				Swap(data + l, data + r);
			l++;
			r--;
		}
	} while (l <= r);
	// make sure m_median doesn't contain any d'tors
	memset(&m_median, 0, sizeof(DATA_T)); 

	if (l < right)
		Sort(data, l, right, direction);
	if (left < r)
		Sort(data, left, r, direction);
}

//-----------------------------------------------------------------------------

void SortDescending(DATA_T* data, int32_t left, int32_t right, tComparator compare)
{
	if (right - left > 1) {
		m_compare = compare;
		Sort(data, left, right, 1);
	}
}

//-----------------------------------------------------------------------------

void SortAscending(DATA_T* data, int32_t left, int32_t right, tComparator compare)
{
	if (right - left > 1) {
		m_compare = compare;
		Sort(data, left, right, -1);
	}
}

// ----------------------------------------------------------------------------
/*
template<typename KEY_T>
int32_t BinSearch(const DATA_T* data, int32_t l, int32_t r, typename KEY_T::KeyType key) 
{
	int32_t	m;

	do {
		m = (l + r) / 2;
		if (data[m] > key)
			r = m - 1;
		else if (data[m] < key)
			l = m + 1;
		else {
			// find first record with equal key
			while ((m > 0) and (data[m - 1] > key))
				--m;
			return m;
		}
	} while (l <= r);
	return -1;
}
*/
// ----------------------------------------------------------------------------

template<typename KEY_T>
int32_t BinSearch(DATA_T* const data, KEY_T const& key, int(__cdecl* compare)(DATA_T const&, KEY_T const&), int32_t l, int32_t r)
{
	int32_t	m;
	int		i;

	do {
		m = (l + r) / 2;
		i = compare(data [m], key);
		if (i > 0)
			r = m - 1;
		else if (i < 0)
			l = m + 1;
		else {
			// find first record with equal key
			while ((m > 0) and (compare (data [m - 1], key) > 0))
				--m;
			return m;
		}
	} while (l <= r);
	return -1;
}

//-----------------------------------------------------------------------------

};
