//
// Created by DietfridMali on 10.01.2025.
//

#ifndef SV_LIST_HELPERS_H
#	define SV_RESULT_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include "SVcomparators.h"

	typedef struct {
		int					typeSize;
		valueComparator_t	comparator;
		char*				value;
		int					capacity;
	} listDescriptor_t;

	listDescriptor_t* create_list(size_t capacity, char* typeName, int typeSize, valueComparator_t comparator);

	bool populate_list(listDescriptor_t* pList, void* values);
		
	void release_list(listDescriptor_t* pList);

	bool list_is_valid(listDescriptor_t* pList);

	int get_list_index(listDescriptor_t* pList, const void* pValue, const void* pTerminator);

	char* get_list_value(listDescriptor_t* pList, size_t i);

#ifdef __cplusplus
}
#endif

#endif // SV_RESULT_STRING_H defined

