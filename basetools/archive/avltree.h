// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#ifndef AVLTREE_H
#	define AVLTREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

	typedef int (*avlDataComparator_t)(const void*, const void*);

	typedef bool (*nodeProcessor_t) (void* const);

	typedef struct avlTreeNode {
		struct avlTreeNode* left;
		struct avlTreeNode* right;
		int8_t				balance;
		void*				value;
	} avlTreeNode_t;

	typedef struct {
		avlTreeNode_t*		root;
		avlTreeNode_t*		current;
		void*				value;
		char*				typeName;
		int					typeSize;
		avlDataComparator_t comparator;
		bool				isDuplicate;
		bool				branchHasChanged;
		bool				deleteValues;
	} avlTreeDescriptor_t;

	// create an avl tree descriptor on the heap and return a pointer to it if everything went alright
	// comparator is a pointer to an appropriate comparison function depending on the ordinal relationship
	// of the data to be stored in the avl tree
	avlTreeDescriptor_t* avltree_create(char* typeName, int typeSize, valueComparator_t comparator);

	// remove the entire avl tree from memory. If deleteValues is true, all data stored in it will also
	// be removed from memory (i.e. freed)
	void avltree_destroy(avlTreeDescriptor_t* avlTree, bool deleteValues);

	// insert a data set *value in the avl tree described by avlTree
	bool avltree_insert(avlTreeDescriptor_t* avlTree, void* value);

	// delete a data set with key *key from the avl tree described by avlTree
	// if deleteValues is true, the data set stored in the avl tree will also be deleted (freed)
	bool avltree_delete(avlTreeDescriptor_t* avlTree, void* key, bool deleteValues);

	// replace a data set with key *oldValue with a data set *newValue
	// if deleteValue is true, the old data set will be deleted (freed)
	bool avltree_replace(avlTreeDescriptor_t* avlTree, void* oldValue, void* newValue, bool deleteValue);

	// return a pointer to the data set with key *key in the avl tree (NULL if not found)
	void* avltree_find(avlTreeDescriptor_t* avlTree, void* key);

	// Walk through all nodes of the avl tree in either ascending or descending order of the data stored therein
	// the function supplied in processNode will be called for each node and a pointer to the data set stored in
	// that node will be passed to it
	bool avltree_walk(avlTreeDescriptor_t* avlTree, nodeProcessor_t processNode, bool reverse);

	// extract the data set with the smallest key from the avl tree (the data set will be removed from the avl tree
	// (but will of course not be deleted ;-)
	void* avltree_extract_min(avlTreeDescriptor_t* avlTree);

	// extract the data set with the biggest key from the avl tree (the data set will be removed from the avl tree
	// (but will of course not be deleted ;-)
	void* avltree_extract_max(avlTreeDescriptor_t* avlTree);

	// check whether the avl tree contains any data
	bool avltree_is_empty(avlTreeDescriptor_t* avlTree);

#ifdef __cplusplus
}
#endif

#endif // AVLTREE_H defined
