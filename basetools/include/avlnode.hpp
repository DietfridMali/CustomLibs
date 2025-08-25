#pragma once

// =================================================================================================

class AVLNode
{
public:
    KEY_T       key;
    DATA_T      data;
    AVLNode*    left;
    AVLNode*    right;
    char        balance;
    int         visited;
#if DEBUG_MALLOC
    int         poolIndex;
#endif

    AVLNode() noexcept
        : left(nullptr), right(nullptr), balance(0), visited(0)
#if DEBUG_MALLOC
        , poolIndex(-1)
#endif
    {
#if !DEBUG_MALLOC
        if constexpr (std::is_trivially_constructible<DATA_T>::value)
            memset(&data, 0, sizeof(DATA_T));
        else
            new (&data) DATA_T();
        if constexpr (std::is_trivially_constructible<KEY_T>::value)
            memset(&key, 0, sizeof(KEY_T));
        else
            new (&key) KEY_T();
#endif
    }

    AVLNode(const KEY_T& k, const DATA_T& d) noexcept(std::is_nothrow_copy_constructible<KEY_T>::value&& std::is_nothrow_copy_constructible<DATA_T>::value)
        : key(k), data(d), left(nullptr), right(nullptr), balance(0), visited(0)
#if DEBUG_MALLOC
        , poolIndex(-1)
#endif
    {
    }

    AVLNode(KEY_T&& k, DATA_T&& d) noexcept(std::is_nothrow_move_constructible<KEY_T>::value&& std::is_nothrow_move_constructible<DATA_T>::value)
        : key(std::move(k)), data(std::move(d)), left(nullptr), right(nullptr), balance(0), visited(0)
#if DEBUG_MALLOC
        , poolIndex(-1)
#endif
    {
    }

    AVLNode* RotateSingleLL(bool isBalanced) noexcept {
        AVLNode* child = left;
        left = child->right;
        child->right = this;
        if (isBalanced) {
            balance = AVL_BALANCED;
            child->balance = AVL_BALANCED;
        }
        else {
            balance = AVL_UNDERFLOW;
            child->balance = AVL_OVERFLOW;
        }
        return child;
    }

    AVLNode* RotateSingleRR(bool isBalanced) noexcept {
        AVLNode* child = right;
        right = child->left;
        child->left = this;
        if (isBalanced) {
            balance = AVL_BALANCED;
            child->balance = AVL_BALANCED;
        }
        else {
            balance = AVL_OVERFLOW;
            child->balance = AVL_UNDERFLOW;
        }
        return child;
    }

    AVLNode* RotateDoubleLR(void) noexcept {
        AVLNode* child = left;
        AVLNode* pivot = child->right;
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

    AVLNode* RotateDoubleRL(void) noexcept {
        AVLNode* child = right;
        AVLNode* pivot = child->left;
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

    inline AVLNode* RotateLeft(bool doSingleRotation, bool isBalanced = true) noexcept {
        return doSingleRotation ? RotateSingleLL(isBalanced) : RotateDoubleLR();
    }

    inline AVLNode* RotateRight(bool doSingleRotation, bool isBalanced = true) noexcept {
        return doSingleRotation ? RotateSingleRR(isBalanced) : RotateDoubleRL();
    }

    inline AVLNode* BalanceLeftGrowth(void) noexcept {
        return RotateLeft(left && left->balance == AVL_UNDERFLOW);
    }

    inline AVLNode* BalanceRightGrowth(void) noexcept {
        return RotateRight(right && right->balance == AVL_OVERFLOW);
    }

    inline AVLNode* BalanceLeftShrink(bool& heightHasChanged) noexcept
    {
        char b = right ? right->balance : AVL_BALANCED;
        if (b != AVL_BALANCED)
            heightHasChanged = false;
        return RotateRight(b != AVL_UNDERFLOW, b != AVL_BALANCED);
    }

    inline AVLNode* BalanceRightShrink(bool& heightHasChanged) noexcept
    {
        char b = left ? left->balance : AVL_BALANCED;
        if (b != AVL_BALANCED)
            heightHasChanged = false;
        return RotateLeft(b != AVL_OVERFLOW, b != AVL_BALANCED);
    }

    inline void SetChild(AVLNode* oldChild, AVLNode* newChild) noexcept {
        if (oldChild == left)
            left = newChild;
        else
            right = newChild;
    }
};

// =================================================================================================
