//
// Created by Dietfrid Mali on 10.01.2025.
//

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "comparators.h"

// compare functions for basic value types
// returns:
// -1: a < b
// +1: a > b
//  0: a == b

int int8_compare(const void* pa, const void* pb) {
	int8_t a = *((int8_t*)pa);
	int8_t b = *((int8_t*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int uint8_compare(const void* pa, const void* pb) {
	uint8_t a = *((uint8_t*)pa);
	uint8_t b = *((uint8_t*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int int16_compare(const void* pa, const void* pb) {
	int16_t a = *((int16_t*)pa);
	int16_t b = *((int16_t*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int uint16_compare(const void* pa, const void* pb) {
	uint16_t a = *((uint16_t*)pa);
	uint16_t b = *((uint16_t*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int int32_compare(const void* pa, const void* pb) {
	int32_t a = *((int32_t*)pa);
	int32_t b = *((int32_t*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int uint32_compare(const void* pa, const void* pb) {
	uint32_t a = *((uint32_t*)pa);
	uint32_t b = *((uint32_t*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int int64_compare(const void* pa, const void* pb) {
	int64_t a = *((int64_t*)pa);
	int64_t b = *((int64_t*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int uint64_compare(const void* pa, const void* pb) {
	uint64_t a = *((uint64_t*)pa);
	uint64_t b = *((uint64_t*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int float_compare(const void* pa, const void* pb) {
	float a = *((float*)pa);
	float b = *((float*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int double_compare(const void* pa, const void* pb) {
	double a = *((double*)pa);
	double b = *((double*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int long_double_compare(const void* pa, const void* pb) {
	long double a = *((long double*)pa);
	long double b = *((long double*)pb);
	return (a < b) ? -1 : (a > b) ? 1 : 0;
}

int string_compare(const void* pa, const void* pb) {
	return strcmp((char*)pa, (char*)pb);
}

// ------------------------------------------------------------------

static comparatorDescriptor_t comparatorDescriptors[] = {
	{ "int8_t", -(int)(sizeof(int8_t)), true, (valueComparator_t)int8_compare },
	{ "uint8_t", sizeof(uint8_t), true, (valueComparator_t)uint8_compare },
	{ "int16_t", -(int)(sizeof(int16_t)), true, (valueComparator_t)int16_compare },
	{ "uint16_t", sizeof(uint16_t), true, (valueComparator_t)uint16_compare },
	{ "int32_t", -(int)(sizeof(int32_t)), true, (valueComparator_t)int32_compare },
	{ "uin32t_t", sizeof(uint32_t), true, (valueComparator_t)uint32_compare },
	{ "int64_t", -(int)(sizeof(int64_t)), true, (valueComparator_t)int64_compare },
	{ "uint64_t", sizeof(uint64_t), true, (valueComparator_t)uint64_compare },
	{ "char", -(int)(sizeof(char)), false, (valueComparator_t)NULL },
	{ "unsigned char",sizeof(unsigned char), false, (valueComparator_t)NULL },
	{ "byte", sizeof(unsigned char), false, (valueComparator_t)NULL },
	{ "short", -(int)(sizeof(short)), false, (valueComparator_t)NULL },
	{ "unsigned short", sizeof(unsigned short), false, (valueComparator_t)NULL },
	{ "ushort", sizeof(unsigned short), false, (valueComparator_t)NULL },
	{ "int", -(int)(sizeof(int)), false, (valueComparator_t)NULL },
	{ "unsigned int", sizeof(unsigned int), false, (valueComparator_t)NULL },
	{ "uint", sizeof(unsigned int), false, (valueComparator_t)NULL },
	{ "long", -(int)(sizeof(long)), false, (valueComparator_t)NULL },
	{ "unsigned long", sizeof(unsigned long), false, (valueComparator_t)NULL },
	{ "ulong", sizeof(unsigned long), false, (valueComparator_t)NULL },
	{ "long long", -(int)(sizeof(long long)), false, (valueComparator_t)NULL },
	{ "unsigned long long", sizeof(unsigned long long), false, (valueComparator_t)NULL },
	{ "ulong long", sizeof(unsigned long long), false, (valueComparator_t)NULL },
	{ "float", -(int)(sizeof(float)), false, (valueComparator_t)float_compare },
	{ "double", -(int)(sizeof(double)), false, (valueComparator_t)double_compare },
	{ "long double", sizeof(long double), false, (valueComparator_t)long_double_compare },
	{ "string", sizeof(char), false, (valueComparator_t)string_compare },
	{ "char*", sizeof(char), false, (valueComparator_t)string_compare },
	{ "size_t", -(int)(sizeof(size_t)), false, (valueComparator_t)NULL },
	{ "ptr_t", sizeof(intptr_t), false, (valueComparator_t)NULL },
	{ NULL, 0 }
};

// ------------------------------------------------------------------
// Return sizeof of data type with name typeName
comparatorDescriptor_t* get_comparator(const char* typeName)
{
	for (comparatorDescriptor_t* p = comparatorDescriptors; p->typeName != NULL; p++)
		if (!strcmp(p->typeName, typeName))
			return p;
	return NULL;
}

// ------------------------------------------------------------------
// Find a comparator from a type with same size and signedness
static valueComparator_t find_matching_comparator(int typeSize)
{
	for (comparatorDescriptor_t* pd = comparatorDescriptors; pd->isSizedInt; pd++) {
		if (pd->typeSize == typeSize) {
			return pd->comparator;
		}
	}
	return NULL;
}

// ------------------------------------------------------------------
// Set up initial module-global data; i.e. find matching comparators for basic integer types
static bool setupNeeded = true;

void setup_comparators(void) 
{
	if (setupNeeded) {
		setupNeeded = false;
		for (comparatorDescriptor_t* pd = comparatorDescriptors; pd->typeName; pd++) {
			if (pd->comparator == NULL) {
				pd->comparator = find_matching_comparator(pd->typeSize);
			}
		}
	}
}

// ------------------------------------------------------------------
// eof
