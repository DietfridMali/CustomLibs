// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

// Generalized avl tree management for arbitrary data types. Data is passed
// as pointers to data buffers (records). Each data packet is assumed to contain data
// that forms a unique key for the record. The user has to supply a compare function
// that can compare two data packets stored in the avl tree using the packets' keys.
// For two record a and b, the compare function has to return -1 if a < b, +1 if a > b,
// and 0 if their keys are equal.
// When queried for data records, the avl tree returns void* pointers to these records;
// these have to by cast to the proper types by the application defining and providing
// these data records.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "comparators.h"
#include "avltree.h"

//-----------------------------------------------------------------------------

#define AVL_OVERFLOW   1
#define AVL_BALANCED   0
#define AVL_UNDERFLOW  3

//-----------------------------------------------------------------------------
// Internal function to search a value in an avl tree. The value (key) pointer is 
// passed in avlTree->value.

static void* find_value(avlTreeDescriptor_t* avlTree)
{
    for (avlTreeNode_t* node = avlTree->root; node; ) {
        switch (avlTree->comparator(avlTree->value, node->value)) {
        case -1:
            node = node->left;
            break;
        case 1:
            node = node->right;
            break;
        default:
            avlTree->current = node;
            return node->value;
        }
    }
    return NULL;
}

//-----------------------------------------------------------------------------
// Allocate a new avl tree node.

static avlTreeNode_t* avltree_alloc_node(avlTreeDescriptor_t* avlTree, void* value)
{
    if (!(avlTree->current = (avlTreeNode_t*)malloc(sizeof (avlTreeNode_t)))) {
        return NULL;
    }
    avlTree->current->value = value;
    avlTree->branchHasChanged = true;
    return avlTree->current;
}

//-----------------------------------------------------------------------------
// Internal function to insert a value in the avl tree. The value pointer is 
// passed in avlTree->value.

static bool insert_value(avlTreeDescriptor_t* avlTree, avlTreeNode_t* root)
{
    avlTreeNode_t* p1, * p2, * r = avlTree->root;
    if (!r)
        return ((root = avltree_alloc_node(avlTree, avlTree->value)) != NULL);

    switch (avlTree->comparator(avlTree->value, r->value)) {
        case -1:
            if (!insert_value(avlTree, r->left))
                return false;
            if (avlTree->branchHasChanged) {
                switch (r->balance) {
                case AVL_UNDERFLOW:
                    p1 = r->left;
                    if (p1->balance == AVL_UNDERFLOW) {  // single LL rotation
                        r->left = p1->right;
                        p1->right = r;
                        r->balance = AVL_BALANCED;
                        r = p1;
                    }
                    else { // double LR rotation
                        p2 = p1->right;
                        p1->right = p2->left;
                        p2->left = p1;
                        r->left = p2->right;
                        p2->right = r;
                        char b = p2->balance;
                        r->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
                        p1->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
                        r = p2;
                    }
                    r->balance = AVL_BALANCED;
                    avlTree->branchHasChanged = false;
                    break;

                case AVL_BALANCED:
                    r->balance = AVL_UNDERFLOW;
                    break;

                case AVL_OVERFLOW:
                    r->balance = AVL_BALANCED;
                    avlTree->branchHasChanged = false;
                    break;
                }
            }
            break;

        case 0:
            avlTree->isDuplicate = true;
            avlTree->current = r;
            break;

        case 1:
            if (!insert_value(avlTree, r->right))
                return false;
            if (avlTree->branchHasChanged) {
                switch (r->balance) {
                case AVL_UNDERFLOW:
                    r->balance = AVL_BALANCED;
                    avlTree->branchHasChanged = false;
                    break;

                case AVL_BALANCED:
                    r->balance = AVL_OVERFLOW;
                    break;

                case AVL_OVERFLOW: {
                    avlTreeNode_t* p1 = r->right;
                    if (p1->balance == AVL_OVERFLOW) { // single RR rotation
                        r->right = p1->left;
                        p1->left = r;
                        r->balance = AVL_BALANCED;
                        r = p1;
                    }
                    else { // double RL rotation
                        avlTreeNode_t* p2 = p1->left;
                        p1->left = p2->right;
                        p2->right = p1;
                        r->right = p2->left;
                        p2->left = r;
                        char b = p2->balance;
                        r->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
                        p1->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
                        r = p2;
                    }
                    r->balance = AVL_BALANCED;
                    avlTree->branchHasChanged = false;
                }
            }
        }
        break;
    }
    root = r;
    return true;
}

//-----------------------------------------------------------------------------
// Rebalance the left sub tree referenced by *root if a record has been removed from it.

static void balance_left_shrink(avlTreeNode_t** root, bool* branchHasShrunk)
{
    avlTreeNode_t* r = *root;
    switch (r->balance) {
        case AVL_UNDERFLOW:
            r->balance = AVL_BALANCED;
            break;

        case AVL_BALANCED:
            r->balance = AVL_OVERFLOW;
            *branchHasShrunk = false;
            break;

        case AVL_OVERFLOW: {
            avlTreeNode_t* p1 = r->right;
            char b = p1->balance;
            if (b != AVL_UNDERFLOW) { // single RR rotation
                r->right = p1->left;
                p1->left = r;
                if (b)
                    r->balance = p1->balance = AVL_BALANCED;
                else {
                    r->balance = AVL_OVERFLOW;
                    p1->balance = AVL_UNDERFLOW;
                    *branchHasShrunk = false;
                }
                r = p1;
            }
            else { // double RL rotation
                avlTreeNode_t* p2 = p1->left;
                b = p2->balance;
                p1->left = p2->right;
                p2->right = p1;
                r->right = p2->left;
                p2->left = r;
                r->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
                p1->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
                r = p2;
                r->balance = AVL_BALANCED;
            }
        }
    }
    *root = r;
}

//-----------------------------------------------------------------------------
// Rebalance the right sub tree referenced by *root if a record has been removed from it.

static void balance_right_shrink(avlTreeNode_t** root, bool* branchHasShrunk)
{
    avlTreeNode_t* r = *root;
    switch (r->balance) {
        case AVL_OVERFLOW:
            r->balance = AVL_BALANCED;
            break;

        case AVL_BALANCED:
            r->balance = AVL_UNDERFLOW;
            *branchHasShrunk = false;
            break;

        case AVL_UNDERFLOW: {
            avlTreeNode_t* p1 = r->left;
            char b = p1->balance;
            if (b != AVL_OVERFLOW) { // single LL rotation
                r->left = p1->right;
                p1->right = r;
                if (b)
                    r->balance = p1->balance = AVL_BALANCED;
                else {
                    r->balance = AVL_UNDERFLOW;
                    p1->balance = AVL_OVERFLOW;
                    *branchHasShrunk = false;
                }
                r = p1;
            }
            else { // double LR rotation
                avlTreeNode_t* p2 = p1->right;
                b = p2->balance;
                p1->right = p2->left;
                p2->left = p1;
                r->left = p2->right;
                p2->right = r;
                r->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
                p1->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
                r = p2;
                r->balance = AVL_BALANCED;
            }
        }
    }
    *root = r;
}

//-----------------------------------------------------------------------------
// reorder_value looks for the record with the biggest key in the right sub tree
// of the record to be removed. That record will take the place of the record to
// be removed. To accomplish that, both records' data pointers will be exchanged.

static void reorder_value(avlTreeNode_t** root, avlTreeNode_t** nodeToDelete, bool* branchHasShrunk)
{
    avlTreeNode_t* r = *root;
    if (r->right) {
        reorder_value(&r->right, nodeToDelete, branchHasShrunk);
        if (*branchHasShrunk)
            balance_right_shrink(&r, branchHasShrunk);
    }
    else {
        avlTreeNode_t* d = *nodeToDelete;
        void* h = r->value;
        r->value = d->value;
        d->value = h;
        *nodeToDelete = r;
        r = r->left;
        *branchHasShrunk = true;
    }
    *root = r;
}

//-----------------------------------------------------------------------------
// Remove node holding data record with key avlTree->value from avl tree.

static bool remove_node(avlTreeDescriptor_t* avlTree, avlTreeNode_t** root, bool* branchHasChanged)
{
    if (!root)
        branchHasChanged = false;
    else {
        avlTreeNode_t* r = *root;
        switch (avlTree->comparator(avlTree->value, r->value)) {
            case -1:
                if (!remove_node(avlTree, &r->left, branchHasChanged))
                    return false;
                if (*branchHasChanged)
                    balance_left_shrink(&r, branchHasChanged);
                break;

            case 1:
                if (!remove_node(avlTree, &r->right, branchHasChanged))
                    return false;
                if (*branchHasChanged)
                    balance_right_shrink(&r, branchHasChanged);
                break;

            default: {
                avlTreeNode_t* d = r;
                if (!r->right) {
                    r = r->left;
                    *branchHasChanged = true;
                }
                else if (!r->left) {
                    r = r->right;
                    *branchHasChanged = true;
                }
                else {
                    reorder_value(&d->left, &d, branchHasChanged);
                    if (*branchHasChanged)
                        balance_left_shrink(&r, branchHasChanged);
                }
                avlTree->value = d->value;
                if (avlTree->deleteValues && d->value) {
                    free(d->value);
                }
                free(d);
            }
        }
        *root = r;
    }
    return true;
}

//-----------------------------------------------------------------------------
// Free all nodes in left and right sub trees of *root, then free root.

static void free_nodes(avlTreeNode_t* root)
{
    if (root) {
        free_nodes(root->left);
        free_nodes(root->right);
        free(root);
    }
}

//-----------------------------------------------------------------------------
// Free all nodes of the avl tree avlTree.

static void avltree_free(avlTreeDescriptor_t* avlTree)
{
    free_nodes(avlTree->root);
}

//-----------------------------------------------------------------------------
// Walk through all nodes of avl sub tree *root, calling processNode in the order
// of the values stored in that sub tree.

static bool walk_nodes_forward(avlTreeNode_t* root, nodeProcessor_t processNode)
{
    if (root) {
        if (!walk_nodes_forward(root->left, processNode))
            return false;
        if (!processNode(root->value))
            return false;
        if (!walk_nodes_forward(root->right, processNode))
            return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
// Walk through all nodes of avl sub tree *root, calling processNode in reverse order
// of the values stored in that sub tree.

static bool walk_nodes_reverse(avlTreeNode_t* root, nodeProcessor_t processNode)
{
    if (root) {
        if (!walk_nodes_reverse(root->right, processNode))
            return false;
        if (!processNode(root->value))
            return false;
        if (!walk_nodes_reverse(root->left, processNode))
            return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
// Walk through an entire avl tree, calling processNode for each value stored in the 
// avl tree either in ascending or descending key order.

bool avltree_walk(avlTreeDescriptor_t* avlTree, nodeProcessor_t processNode, bool reverse)
{
    return reverse ? walk_nodes_reverse(avlTree->root, processNode) : walk_nodes_forward(avlTree->root, processNode);
} /*AvlWalk*/

// ------------------------------------------------------------------
// Create an avl tree and return a pointer to its descriptor.

avlTreeDescriptor_t* avltree_create(char* typeName, int typeSize, valueComparator_t comparator)
{
    avlTreeDescriptor_t* avlTree = malloc(sizeof(avlTreeDescriptor_t));
    if (avlTree != NULL) {
        memset(avlTree, 0, sizeof(avlTreeDescriptor_t));
        avlTree->typeName = typeName;
        avlTree->typeSize = typeSize;
        avlTree->comparator = comparator;
    }
    return avlTree;
}

//-----------------------------------------------------------------------------
// Insert a value referenced by value in the avl tree avlTree.

bool avltree_insert(avlTreeDescriptor_t* avlTree, void* value)
{
    avlTree->branchHasChanged = avlTree->isDuplicate = false;
    avlTree->value = value;
    if (!insert_value(avlTree, avlTree->root))
        return false;
    if (!avlTree->isDuplicate) {
        avlTree->current->value = value;
    }
    return true;
}

//-----------------------------------------------------------------------------
// Delete a value the key of which is passed in value from the avl tree avlTree.

bool avltree_delete(avlTreeDescriptor_t* avlTree, void* key, bool deleteValues)
{
    if (!(avlTree && avlTree->root))
        return false;
    avlTree->value = key;
    avlTree->branchHasChanged = false;
    avlTree->deleteValues = deleteValues;
    return remove_node(avlTree, &avlTree->root, &avlTree->branchHasChanged);
    free(avlTree);
}

//-----------------------------------------------------------------------------
// Free all nodes in left and right sub trees of *root, then free root.

static void destroy_nodes(avlTreeNode_t* root, bool deleteValues)
{
    if (root) {
        destroy_nodes(root->left, deleteValues);
        destroy_nodes(root->right, deleteValues);
        if (deleteValues && (root->value)) {
            free(root->value);
        }
        free(root);
    }
}

//-----------------------------------------------------------------------------
// Delete an entire avlTree. Free all nodes of the avl tree avlTree. Then free avlTree.
// If deleteValues is true, the values stored in the avl tree will be freed, too.

void avltree_destroy(avlTreeDescriptor_t* avlTree, bool deleteValues)
{
    if (!avltree_is_empty(avlTree)) {
        destroy_nodes(avlTree->root, deleteValues);
        free(avlTree);
    }
}

//-----------------------------------------------------------------------------
// Replace a value in an avl tree with another value.

bool avltree_replace(avlTreeDescriptor_t* avlTree, void* oldValue, void* newValue, bool deleteValue)
{
    if (!avltree_delete(avlTree, oldValue, deleteValue))
        return false;
    if (!avltree_insert(avlTree, newValue))
        return false;
    return true;
}

//-----------------------------------------------------------------------------
// Find the value with key key in the avl tree avlTree.

static void* avltree_find(avlTreeDescriptor_t* avlTree, void* key)
{
    avlTree->value = key;
    return find_value(avlTree);
}
 
//-----------------------------------------------------------------------------
// Return the value with the smallest key stored in avl tree avlTree.

static void* avltree_get_min(avlTreeDescriptor_t* avlTree)
{
    if (!avlTree->root)
        return NULL;
    avlTreeNode_t* p = avlTree->root;
    for (; p->left; p = p->left)
        ;
    return p->value;
}

//-----------------------------------------------------------------------------
// Return the value with the biggest key stored in avl tree avlTree.

static void* avltree_get_max(avlTreeDescriptor_t* avlTree)
{
    if (!avlTree->root)
        return NULL;
    avlTreeNode_t* p = avlTree->root;
    for (; p->right; p = p->right)
        ;
    return p->value;
}

//-----------------------------------------------------------------------------
// Internal function to Return the value with the biggest key stored in avl tree avlTree and remove it from the avl tree.

static void extract_min_value(avlTreeDescriptor_t* avlTree, avlTreeNode_t** root, bool* branchHasChanged)
{
    avlTreeNode_t* r = *root;

    if (!r)
        avlTree->branchHasChanged = false;
    else if (r->left) {
        extract_min_value(avlTree, &r->left, branchHasChanged);
        if (*branchHasChanged)
            balance_left_shrink(&r, branchHasChanged);
    }
    else {
        avlTreeNode_t* d = r;
        avlTree->value = r->value;
        free (d);
        *branchHasChanged = true;
    }
    *root = r;
}

//-----------------------------------------------------------------------------
// Return the value with the smallest key stored in avl tree avlTree and remove it from the avl tree.

void* avltree_extract_min(avlTreeDescriptor_t* avlTree)
{
    if (!avlTree->root)
        return NULL;
    avlTree->branchHasChanged = false;
    extract_min_value(avlTree, &avlTree->root, &avlTree->branchHasChanged);
    return avlTree->value;
}

//-----------------------------------------------------------------------------
// Internal function to return the value with the biggest key stored in avl tree avlTree and remove it from the avl tree.

static void extract_max_value(avlTreeDescriptor_t* avlTree, avlTreeNode_t** root, bool* branchHasChanged)
{
    avlTreeNode_t* r = *root;

    if (!r)
        avlTree->branchHasChanged = false;
    else if (r->right) {
        extract_max_value(avlTree, &r->right, branchHasChanged);
        if (*branchHasChanged)
            balance_right_shrink(&r, branchHasChanged);
    }
    else {
        avlTreeNode_t* d = r;
        avlTree->value = r->value;
        free (d);
        *branchHasChanged = true;
    }
    *root = r;
}

//-----------------------------------------------------------------------------
// Return the value with the biggest key stored in avl tree avlTree and remove it from the avl tree.

void* avltree_extract_max(avlTreeDescriptor_t* avlTree)
{
    if (!avlTree->root)
        return NULL;
    avlTree->branchHasChanged = false;
    extract_max_value(avlTree, &avlTree->root, &avlTree->branchHasChanged);
    return avlTree->value;
}

// ------------------------------------------------------------------
// Check whether the avl tree avlTree is empty (and valid).

bool avltree_is_empty(avlTreeDescriptor_t* avlTree)
{
    return ((avlTree != NULL) && (avlTree->root != NULL));
}

// ------------------------------------------------------------------
// eof