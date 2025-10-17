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

    //-----------------------------------------------------------------------------

    inline AVLNode* BalanceLeftGrowth() noexcept
    {
        // balance == AVL_UNDERFLOW
        AVLNode* child = left;
        char b = child->balance;

        if (b == AVL_UNDERFLOW) {                // single LL
            left = child->right;
            child->right = this;
            balance = AVL_BALANCED;
            child->balance = AVL_BALANCED;
            return child;
        }

        // double LR (lb == AVL_OVERFLOW)
        AVLNode* pivot = child->right;
        child->right = pivot->left;
        pivot->left = child;
        left = pivot->right;
        pivot->right = this;

         b = pivot->balance;
        balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
        child->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
        pivot->balance = AVL_BALANCED;
        return pivot;
    }

    //-----------------------------------------------------------------------------

    inline AVLNode* BalanceRightGrowth() noexcept
    {
        // balance == AVL_OVERFLOW
        AVLNode* child = right;
        char b = child->balance;

        if (b == AVL_OVERFLOW) {                 // single RR
            right = child->left;
            child->left = this;
            balance = AVL_BALANCED;
            child->balance = AVL_BALANCED;
            return child;
        }

        // double RL (rb == AVL_UNDERFLOW)
        AVLNode* pivot = child->left;
        child->left = pivot->right;
        pivot->right = child;
        right = pivot->left;
        pivot->left = this;

        b = pivot->balance;
        balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
        child->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
        pivot->balance = AVL_BALANCED;
        return pivot;
    }

    //-----------------------------------------------------------------------------

    inline AVLNode* BalanceLeftShrink(bool& heightHasChanged) noexcept
    {
        AVLNode* child = right;
        char b = child->balance;

        if (b != AVL_UNDERFLOW) { // single RR
            right = child->left;
            child->left = this;
            if (b == AVL_BALANCED) {
                balance = AVL_OVERFLOW;
                child->balance = AVL_UNDERFLOW;
                heightHasChanged = false;
            }
            else {
                balance = AVL_BALANCED;
                child->balance = AVL_BALANCED;
            }
            return child;
        }

        // double RL
        AVLNode* pivot = child->left;
        child->left = pivot->right;
        pivot->right = child;
        right = pivot->left;
        pivot->left = this;

        b = pivot->balance;
        balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
        child->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
        pivot->balance = AVL_BALANCED;
        return pivot;
    }

    //-----------------------------------------------------------------------------

    inline AVLNode* BalanceRightShrink(bool& heightHasChanged) noexcept
    {
        AVLNode* child = left;
        char b = child->balance;

        if (b != AVL_OVERFLOW) { // single LL
            left = child->right;
            child->right = this;
            if (b == AVL_BALANCED) {
                balance = AVL_UNDERFLOW;
                child->balance = AVL_OVERFLOW;
                heightHasChanged = false;
            }
            else {
                balance = AVL_BALANCED;
                child->balance = AVL_BALANCED;
            }
            return child;
        }

        // double LR
        AVLNode* pivot = child->right;        // <- war: child->left (falsch)
        child->right = pivot->left;
        pivot->left = child;
        left = pivot->right;        // <- war: right = ... (falsch)
        pivot->right = this;

        b = pivot->balance;
        balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
        child->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
        pivot->balance = AVL_BALANCED;
        return pivot;
    }

    inline void SetChild(AVLNode* oldChild, AVLNode* newChild) noexcept {
        if (oldChild == left)
            left = newChild;
        else
            right = newChild;
    }
};

// =================================================================================================
