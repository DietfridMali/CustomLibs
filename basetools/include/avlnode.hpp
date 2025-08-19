#pragma once

#include "avltreetraits.h"

// =================================================================================================

#define AVL_OVERFLOW   1
#define AVL_BALANCED   0
#define AVL_UNDERFLOW  -1

//-----------------------------------------------------------------------------

class AVLNode;

using AVLNodePtr = AVLNode*;

//-----------------------------------------------------------------------------

class AVLNode
{
public:
    KEY_T		    key;
    DATA_T		    data;
    AVLNodePtr      left;
    AVLNodePtr      right;
    char		    balance;
    int             visited;
#if DEBUG_MALLOC
    int             poolIndex;
#endif

        AVLNode()
            : left(nullptr), right(nullptr), balance(0), visited(0)
#if DEBUG_MALLOC
            , poolIndex(-1)
#endif
        {
#if !DEBUG_MALLOC
            if constexpr (not std::is_trivially_constructible<DATA_T>::value)
                memset(&data, 0, sizeof(DATA_T));
            else
                new(&data) DATA_T();
            if constexpr (not std::is_trivially_constructible<KEY_T>::value)
                memset(&key, 0, sizeof(KEY_T));
            else
                new(&key) DATA_T();
#endif
        }

        AVLNode(KEY_T key, DATA_T data)
            : key(key), data(data), balance(0), visited(0)
#if DEBUG_MALLOC
            , poolIndex(-1)
#endif
        {
 }

    AVLNodePtr RotateSingleLL(bool isBalanced) {
        AVLNodePtr child = left;
        left = child->right;
        child->right = this;
        if (isBalanced) { // always true for insertions
            balance =
            child->balance = AVL_BALANCED;
        }
        else {
            balance = AVL_UNDERFLOW;
            child->balance = AVL_OVERFLOW;
        }
        return child;
    }


    AVLNodePtr RotateSingleRR(bool isBalanced) {
        AVLNodePtr child = right;
        right = child->left;
        child->left = this;
        if (isBalanced) { // always true for insertions
            balance =
            child->balance = AVL_BALANCED;
        }
        else {
            balance = AVL_OVERFLOW;
            right->balance = AVL_UNDERFLOW;
        }
        return child;
    }


    AVLNodePtr RotateDoubleLR(void) {
        AVLNodePtr child = left;
        AVLNodePtr pivot = child->right;
        child->right = pivot->left;
        pivot->left = child;
        left = pivot->right;
        pivot->right = this;
        char b = pivot->balance;
        balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
        child->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
        pivot->balance = AVL_BALANCED;
        return pivot;
    }


    AVLNodePtr RotateDoubleRL(void) {
        AVLNodePtr child = right;
        AVLNodePtr pivot = child->left;
        child->left = pivot->right;
        pivot->right = child;
        right = pivot->left;
        pivot->left = this;
        char b = pivot->balance;
        balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
        child->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
        pivot->balance = AVL_BALANCED;
        return pivot;
    }


    inline AVLNodePtr RotateLeft(bool doSingleRotation, bool isBalanced = true) {
        return doSingleRotation ? RotateSingleLL(isBalanced) : RotateDoubleLR();
    }


    inline AVLNodePtr RotateRight(bool doSingleRotation, bool isBalanced = true) {
        return doSingleRotation ? RotateSingleRR(isBalanced) : RotateDoubleRL();
    }


    inline AVLNodePtr BalanceLeftGrowth(void) {
        return RotateLeft(left->balance == AVL_UNDERFLOW);
    }


    inline AVLNodePtr BalanceRightGrowth(void) {
        return RotateRight(right->balance == AVL_OVERFLOW);
    }


    inline AVLNodePtr BalanceLeftShrink(bool& heightHasChanged)
    {
        char b = right->balance;
        if (b != AVL_BALANCED)
            heightHasChanged = false;
        return RotateRight(b != AVL_UNDERFLOW, b != AVL_BALANCED);
    }


    inline AVLNodePtr BalanceRightShrink(bool& heightHasChanged)
    {
        char b = left->balance;
        if (b != AVL_BALANCED)
            heightHasChanged = false;
        return RotateLeft(b != AVL_OVERFLOW, b != AVL_BALANCED);
    }


    inline void SetChild(AVLNodePtr oldChild, AVLNodePtr newChild) {
        // it is assured that either left or right indeed point to the old child
        if (oldChild == left)
            left = newChild;
        else // if (oldChild == right) 
            right = newChild;
    }
};

// =================================================================================================

