#pragma once

//-----------------------------------------------------------------------------

template < class DATA_T > 
class CQuickSort {
	public:
		typedef int (CQuickSort<DATA_T>::*tComparator) (const DATA_T*, const DATA_T*);
/*
		void SortAscending (DATA_T* buffer, size_t left, size_t right);
		void SortDescending (DATA_T* buffer, size_t left, size_t right);
		void SortAscending (DATA_T* buffer, size_t left, size_t right, comparator compare);
		void SortDescending (DATA_T* buffer, size_t left, size_t right, comparator compare);
		inline void Swap (DATA_T* left, DATA_T* right);
		size_t BinSearch (DATA_T* buffer, size_t l, size_t r, DATA_T key);
*/
//-----------------------------------------------------------------------------

inline void Swap(DATA_T* left, DATA_T* right)
{
	DATA_T h = *left;
	*left = *right;
	*right = h;
}

//-----------------------------------------------------------------------------

void SortAscending(DATA_T* buffer, size_t left, size_t right)
{
	size_t	l = left,
			r = right;
	DATA_T		median = buffer[(l + r) / 2];

	do {
		while (buffer[l] < median)
			l++;
		while (buffer[r] > median)
			r--;
		if (l <= r) {
			if (l < r)
				Swap(buffer + l, buffer + r);
			l++;
			r--;
		}
	} while (l <= r);
	if (l < right)
		SortAscending(buffer, l, right);
	if (left < r)
		SortAscending(buffer, left, r);
}

//-----------------------------------------------------------------------------

void SortDescending(DATA_T* buffer, size_t left, size_t right)
{
	size_t	l = left,
		r = right;
	DATA_T		median = buffer[(l + r) / 2];

	do {
		while (buffer[l] > median)
			l++;
		while (buffer[r] < median)
			r--;
		if (l <= r) {
			if (l < r)
				Swap(buffer + l, buffer + r);
			l++;
			r--;
		}
	} while (l <= r);
	if (l < right)
		SortDescending(buffer, l, right);
	if (left < r)
		SortDescending(buffer, left, r);
}

//-----------------------------------------------------------------------------

void SortAscending(DATA_T* buffer, size_t left, size_t right, tComparator compare)
{
	size_t	l = left,
			r = right;
	DATA_T		median = buffer[(l + r) / 2];

	do {
		while (compare(buffer + l, &median) < 0)
			l++;
		while (compare(buffer + r, &median) > 0)
			r--;
		if (l <= r) {
			if (l < r)
				Swap(buffer + l, buffer + r);
			l++;
			r--;
		}
	} while (l <= r);
	if (l < right)
		SortAscending(buffer, l, right, compare);
	if (left < r)
		SortAscending(buffer, left, r, compare);
}

//-----------------------------------------------------------------------------

void SortDescending(DATA_T* buffer, size_t left, size_t right, tComparator compare)
{
	size_t	l = left,
			r = right;
	DATA_T		m = buffer[(l + r) / 2];

	do {
		while (compare(buffer + l, &m) > 0)
			l++;
		while (compare(buffer + r, &m) < 0)
			r--;
		if (l <= r) {
			if (l < r)
				Swap(buffer + l, buffer + r);
			l++;
			r--;
		}
	} while (l <= r);
	if (l < right)
		SortDescending(buffer, l, right, compare);
	if (left < r)
		SortDescending(buffer, left, r, compare);
}

// ----------------------------------------------------------------------------

size_t BinSearch(DATA_T* buffer, size_t l, size_t r, DATA_T key) 
{
	size_t	m;

	do {
		m = (l + r) / 2;
		if (key < buffer[m])
			r = m - 1;
		else if (key > buffer[m])
			l = m + 1;
		else {
			// find first record with equal key
			for (; m > 0; m--)
				if (key > buffer[m - 1])
					break;
			return m;
		}
	} while (l <= r);
	return -1;
}

//-----------------------------------------------------------------------------

};
