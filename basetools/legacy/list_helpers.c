//
// Created by DietfridMali on 10.01.2025.
//

#include "SVresult_helpers.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "SVcomparators.h"
#include "SVstring_helpers.h"
#include "SVlist_helpers.h"

/* Helper functions for handling fixed size lists of any datatype.
*/

// ------------------------------------------------------------------
// Return sizeof of data type with name typeName
static comparatorDescriptor_t* get_type_descriptor(char* typeName) 
{
	for (comparatorDescriptor_t* p = comparatorDescriptors; p->typeName != NULL; p++)
		if (!strcmp(p->typeName, typeName))
			return p;
	return NULL;
}

// ------------------------------------------------------------------
// Return sizeof of data type with name typeName
static size_t get_type_index(char* typeName) 
{
	comparatorDescriptor_t* p = get_type_descriptor(typeName);
	return (p ? p - comparatorDescriptors : 0);
}

// ------------------------------------------------------------------
// Return sizeof of data type with name typeName
static size_t get_type_size(char* typeName) 
{
	comparatorDescriptor_t* p = get_type_descriptor(typeName);
	return (p ? p->typeSize : 0);
}

// ------------------------------------------------------------------
// Setup a list; for custom types, pass typeSize and a fitting comparator function
listDescriptor_t* create_list(size_t capacity, char* typeName, int typeSize, valueComparator_t comparator)
{
	setup_comparators();
	comparatorDescriptor_t* pd = get_type_descriptor(typeName);
	if (typeSize == 0) {
		if (pd)
			typeSize = pd->typeSize;
		if (typeSize == 0) {
			return NULL;
		}
	}
	listDescriptor_t* pList = (listDescriptor_t*)malloc(sizeof(listDescriptor_t));
	if (!pList)
		return NULL;
	if (comparator != NULL) {
		pList->comparator = comparator;
	}
	else if (!(pList->comparator = pd->comparator)) {
		free(pList);
		return NULL;
	}
	pList->typeSize = (typeSize < 0) ? -typeSize : typeSize;
	if (!(pList->value = malloc(capacity * typeSize))) {
		free(pList);
		return NULL;
	}
	return pList;
}

// ------------------------------------------------------------------
// Copy values into the list's value buffer
// Alternatively, just set pList->value = values if no copy is needed
bool populate_list(listDescriptor_t* pList, void* values) 
{
	if (!list_is_valid(pList)) {
		return false;
	}
	memcpy(pList->value, values, pList->capacity * pList->typeSize);
	return true;
}

// ------------------------------------------------------------------
// Free list descriptor and value buffer
void release_list(listDescriptor_t* pList) 
{
	if (pList) {
		if (pList->value) {
			free(pList->value);
		}
		free(pList);
	}
}

// ------------------------------------------------------------------
// Determine whether the list descriptor has been properly initialized
bool list_is_valid(listDescriptor_t* pList) 
{
	return (pList && pList->value && pList->typeSize && pList->comparator);
}

// ------------------------------------------------------------------
// Find value *pValue in value buffer of *pList and return its index in the buffer
// return -1 if not present in the list
// *pTerminator points at a value that is used to indicate the end of the list
// So the last element of a list must contain that value
int get_list_index (listDescriptor_t* pList, const void* pValue, const void* pTerminator) 
{
	if (list_is_valid (pList)) {
		for (char* p = (char*)pList->value; pList->comparator(p, pTerminator); p += pList->typeSize) {
			if (!pList->comparator(p, pValue)) {
				return (int) ((p - (char*)pList->value) / pList->typeSize);
			}
		}
	}
	return -1;
}

// ------------------------------------------------------------------
// Return the index of the element of textList equal to text if that exists,
char* get_list_value(listDescriptor_t* pList, size_t i)
{
	return (i < 0) ? NULL : (char*)pList->value + i * pList->typeSize;
}

// ------------------------------------------------------------------

// eof
