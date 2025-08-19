// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#ifndef COMPARATORS_H
#	define COMPARATORS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef int (*valueComparator_t)(const void*, const void*);

	typedef struct {
		char* typeName;
		int typeSize; // negative for signed types, otherwise positive
		bool isSizedInt; // [u]int##_t
		valueComparator_t comparator;
	} comparatorDescriptor_t;

	extern comparatorDescriptor_t comparatorDescriptors[];

	void setup_comparators(void);

	int int8_compare(const void* pa, const void* pb);

	int uint8_compare(const void* pa, const void* pb);

	int int16_compare(const void* pa, const void* pb);

	int uint16_compare(const void* pa, const void* pb);

	int int32_compare(const void* pa, const void* pb);

	int uint32_compare(const void* pa, const void* pb);

	int int64_compare(const void* pa, const void* pb);

	int uint64_compare(const void* pa, const void* pb);

	int float_compare(const void* pa, const void* pb);

	int double_compare(const void* pa, const void* pb);

	int long_double_compare(const void* pa, const void* pb);

	int string_compare(const void* pa, const void* pb);

	comparatorDescriptor_t* get_comparator(const char* typeName);

#ifdef __cplusplus
}
#endif

#endif // COMPARATORS_H defined
