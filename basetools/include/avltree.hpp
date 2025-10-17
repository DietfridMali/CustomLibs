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

#pragma once

#include <utility>
#include <stdexcept>
#include <functional>
#include <type_traits>
#include <cstring>
#include "avltreetraits.h"
#include "type_helper.hpp"

// =================================================================================================

#define AVL_OVERFLOW   1
#define AVL_BALANCED   0
#define AVL_UNDERFLOW  -1

#define RELINK_DELETED_NODE 0

// =================================================================================================

template <typename KEY_T, typename DATA_T>
class AVLTree
{
public:
    using Comparator = typename AVLTreeTraits<KEY_T, DATA_T>::Comparator;
    using DataProcessor = typename AVLTreeTraits<KEY_T, DATA_T>::DataProcessor;

    //-----------------------------------------------------------------------------

#include "avlnode.hpp"

//-----------------------------------------------------------------------------

private:
    struct tAVLTreeInfo {
        AVLNode*    root;
        AVLNode*    workingNode;
        AVLNode*    workingParent;
        int         nodeCount;
        KEY_T       workingKey;
        DATA_T      workingData;
        Comparator  compareNodes;
        std::function<bool(const KEY_T&, DATA_T*)>  processNode;
        void*       context;
        int         visited;
        bool        isDuplicate;
        bool        heightHasChanged;
        bool        result;
#ifdef _DEBUG
        AVLNode*    testNode;
        KEY_T       nullKey;
        KEY_T       testKey;
#endif

        tAVLTreeInfo() noexcept
            : root(nullptr)
            , workingNode(nullptr)
            , workingParent(nullptr)
            , nodeCount(0)
            , compareNodes(nullptr)
            , processNode(nullptr)
            , context(nullptr)
            , visited(0)
            , isDuplicate(false)
            , heightHasChanged(false)
            , result(false)
#ifdef _DEBUG
            , testNode(nullptr)
#endif
        {
            InitializeAnyType(workingData);
            InitializeAnyType(workingKey);
#ifdef _DEBUG
            InitializeAnyType(nullKey);
            InitializeAnyType(testKey);
#endif
        }
    };

private:
    tAVLTreeInfo            m_info;

    //-----------------------------------------------------------------------------

public:

    AVLTree(int capacity = 0) noexcept
    {
    }

    ~AVLTree() {
        Destroy();
    }

    inline void SetComparator(Comparator compareNodes, void* context = nullptr) noexcept {
        m_info.compareNodes = compareNodes;
        m_info.context = context;
    }

    inline int Size(void) const noexcept {
        return m_info.nodeCount;
    }

    //-----------------------------------------------------------------------------

public:
    DATA_T* Find(const KEY_T& key)
        noexcept(noexcept(m_info.compareNodes(m_info.context, key, std::declval<const KEY_T&>())))
    {
        if (not m_info.root)
            return nullptr;
        for (AVLNode* node = m_info.root; node != nullptr; ) {
            int rel = m_info.compareNodes(m_info.context, key, node->key);
            if (rel < 0)
                node = node->left;
            else if (rel > 0)
                node = node->right;
            else {
                m_info.workingNode = node;
                return &node->data;
            }
        }
        return nullptr;
    }

    inline DATA_T* Find(KEY_T&& key)
        noexcept(noexcept(Find(static_cast<const KEY_T&>(key))))
    {
        return Find(static_cast<const KEY_T&>(key));
    }

    //-----------------------------------------------------------------------------

public:
    AVLTree<KEY_T, DATA_T>::AVLNode* FindData(const DATA_T& data, AVLNode* node = nullptr, bool start = true) noexcept
    {
        if (start) {
            node = m_info.root;
            m_info.workingNode = nullptr;
            ++m_info.visited;
        }
        if (not node)
            return nullptr;
        if (node) {
            if (node->visited == m_info.visited)
                return nullptr;
            node->visited = m_info.visited;
            if (FindData(data, node->left, false))
                return m_info.workingNode;
            if (node->data == data)
                return m_info.workingNode = node;
            if (FindData(data, node->right, false))
                return m_info.workingNode;
        }
        return nullptr;
    }

    //-----------------------------------------------------------------------------

public:
    bool Extract(const KEY_T& key, DATA_T& data)
    {
        if (not Remove(key))
            return false;
        data = std::move(m_info.workingData);
        return true;
    }

    inline bool Extract(KEY_T&& key, DATA_T& data) {
        return Extract(static_cast<const KEY_T&>(key), data);
    }

    //-----------------------------------------------------------------------------

private:
    AVLNode* AllocNode(void)
    {
        m_info.workingNode = new AVLNode();
        m_info.workingNode->key = std::move(m_info.workingKey);
        ++m_info.nodeCount;
        return m_info.workingNode;
    }

    //-----------------------------------------------------------------------------

    void DeleteNode(AVLNode*& node) noexcept {
        delete node;
        node = nullptr;
        --m_info.nodeCount;
    }

    //-----------------------------------------------------------------------------

public:
    bool CheckForNullKey(AVLNode* root, bool start = true) noexcept {
        return false;
        if (start) {
            m_info.testKey = m_info.nullKey;
            ++m_info.visited;
        }
        if (root) {
            if (root->visited == m_info.visited)
                return false;
            root->visited = m_info.visited;
            if (not CheckForNullKey(root->left, false))
                return false;
            if constexpr (std::is_same<DATA_T, int>::value) {
                if (root->data == 0) {
                    if (m_info.testKey == m_info.nullKey)
                        m_info.testKey = root->key;
                    else
                        return false;
                }
            }
            if (not m_info.compareNodes(m_info.context, m_info.nullKey, root->key))
                return false;
            if (not CheckForNullKey(root->right, false))
                return false;
        }
        return true;
    }

    bool CheckForCycles(AVLNode* node = nullptr, bool start = true) noexcept {
        return false;
        if (start) {
            node = m_info.root;
            ++m_info.visited;
        }
        if (node) {
            if (node->visited == m_info.visited)
                return false;
            node->visited = m_info.visited;
            if (not CheckForCycles(node->left, false))
                return false;
            if (not CheckForCycles(node->right, false))
                return false;
        }
        return true;
    }

    //-----------------------------------------------------------------------------

private:
    inline AVLNode* BalanceLeftGrowth(AVLNode* node) noexcept
    {
        switch (node->balance) {
        case AVL_UNDERFLOW:
            m_info.heightHasChanged = false;
            return node->BalanceLeftGrowth();
        case AVL_BALANCED:
            node->balance = AVL_UNDERFLOW;
            return node;
        case AVL_OVERFLOW:
            m_info.heightHasChanged = false;
            node->balance = AVL_BALANCED;
            return node;
        }
    }

    //-----------------------------------------------------------------------------

private:
    inline AVLNode* BalanceRightGrowth(AVLNode* node) noexcept
    {
        switch (node->balance) {
        case AVL_OVERFLOW:
            m_info.heightHasChanged = false;
            return node->BalanceRightGrowth();
        case AVL_BALANCED:
            node->balance = AVL_OVERFLOW;
            return node;
        case AVL_UNDERFLOW:
            m_info.heightHasChanged = false;
            node->balance = AVL_BALANCED;
            return node;
        }
    }

    //-----------------------------------------------------------------------------

private:
    AVLNode* InsertNode(AVLNode* node, AVLNode* parent = nullptr)
    {
        if (not node) {
            if (not (m_info.workingNode = AllocNode()))
                return nullptr;
            m_info.heightHasChanged = true;
            return m_info.workingNode;
        }

        int rel = m_info.compareNodes(m_info.context, m_info.workingKey, node->key);
        if (rel < 0) {
            if (not (node->left = InsertNode(node->left, node)))
                return node;
            if (m_info.heightHasChanged) {
                switch (node->balance) {
                case AVL_UNDERFLOW:
                    m_info.heightHasChanged = false;
                    return node->BalanceLeftGrowth();
                case AVL_BALANCED:
                    node->balance = AVL_UNDERFLOW;
                    return node;
                case AVL_OVERFLOW:
                    m_info.heightHasChanged = false;
                    node->balance = AVL_BALANCED;
                    return node;
                }
            }
        }
        else if (rel > 0) {
            if (not (node->right = InsertNode(node->right, node)))
                return node;
            if (m_info.heightHasChanged) {
                switch (node->balance) {
                case AVL_OVERFLOW:
                    m_info.heightHasChanged = false;
                    return node->BalanceRightGrowth();
                case AVL_BALANCED:
                    node->balance = AVL_OVERFLOW;
                    return node;
                case AVL_UNDERFLOW:
                    m_info.heightHasChanged = false;
                    node->balance = AVL_BALANCED;
                    return node;
                }
            }
        }
        else {
            m_info.isDuplicate = true;
            m_info.workingNode = node;
            m_info.heightHasChanged = false;
        }
        return node;
    }

    //-----------------------------------------------------------------------------

public:
    template<typename K = KEY_T, typename D = DATA_T>
        requires std::constructible_from<KEY_T, K&&>&& std::constructible_from<DATA_T, D&&>
    bool Insert(K&& key, D&& data, bool updateData = false)
    {
        m_info.workingKey = std::forward<K>(key);
        m_info.heightHasChanged = false;
        m_info.isDuplicate = false;
        m_info.workingNode = nullptr;
        m_info.root = InsertNode(m_info.root);
        if (not m_info.workingNode)
            return false;
        if (not m_info.isDuplicate or updateData)
            m_info.workingNode->data = std::forward<D>(data);
        return true;
    }

    bool Insert2(const KEY_T& key, const DATA_T& data, const KEY_T& nullKey, bool updateData = false)
    {
        m_info.workingKey = key;
        m_info.nullKey = nullKey;
        m_info.testKey = nullKey;
        m_info.heightHasChanged = false;
        m_info.isDuplicate = false;
        m_info.root = InsertNode(m_info.root);
#if AVL_DEBUG
        CheckForCycles(m_info.root, true);
#endif
        if (not m_info.workingNode)
            return false;
        if (not m_info.isDuplicate)
            m_info.workingNode->data = data;
        else if (updateData)
            m_info.workingNode->data = data;
        return true;
    }

    //-----------------------------------------------------------------------------

private:
    AVLNode* BalanceLeftShrink(AVLNode* node) noexcept
    {
        switch (node->balance) {
        case AVL_UNDERFLOW:
            node->balance = AVL_BALANCED;
            return node;
        case AVL_BALANCED:
            node->balance = AVL_OVERFLOW;
            m_info.heightHasChanged = false;
            return node;
        default:
            return node->BalanceLeftShrink(m_info.heightHasChanged);
        }
    }

    //-----------------------------------------------------------------------------

private:
    AVLNode* BalanceRightShrink(AVLNode* node) noexcept
    {
        switch (node->balance) {
        case AVL_OVERFLOW:
            node->balance = AVL_BALANCED;
            return node;
        case AVL_BALANCED:
            node->balance = AVL_UNDERFLOW;
            m_info.heightHasChanged = false;
            return node;
        default:
            return node->BalanceRightShrink(m_info.heightHasChanged);
        }
    }

    //-----------------------------------------------------------------------------

#if RELINK_DELETED_NODE
    void SwapNodes(AVLNode* delParent, AVLNode* delNode, AVLNode* replParent, AVLNode* replNode) noexcept {
        // delNode im Eltern verankern (Root-Fall)
        if (delParent) 
            delParent->SetChild(delNode, replNode);
        else           
            m_info.root = replNode;

        if (replParent == delNode) {
            replNode->right = delNode->right;
            // replNode->left bleibt wie vorher (kein Zyklus erzeugen)
        }
        else {
            replParent->right = replNode->left; // replNode war rechter Ast des replParent
            replNode->left = delNode->left;
            replNode->right = delNode->right;
        }
        replNode->balance = delNode->balance;
    }
#endif

    //-----------------------------------------------------------------------------

    AVLNode* UnlinkNode(
        AVLNode* node
#if RELINK_DELETED_NODE
        , AVLNode* parent
#endif
    )
    {
        if (node->right) {
            node->right =
#if RELINK_DELETED_NODE
                UnlinkNode(node->right, node);
#else
                UnlinkNode(node->right);
#endif
            return m_info.heightHasChanged ? BalanceRightShrink(node) : node;
        }
        else {
            m_info.heightHasChanged = true;
            m_info.result = true;
#if RELINK_DELETED_NODE
            SwapNodes(m_info.workingParent, m_info.workingNode, parent, node);
            if (parent != m_info.workingParent)
                parent = BalanceRightShrink(parent);
            return parent->right;
#else
            std::swap(m_info.workingNode->key, node->key);
            std::swap(m_info.workingNode->data, node->data);
            m_info.workingNode = node;
#if AVL_DEBUG
            CheckForCycles(m_info.root, true);
#endif
            return node->left;
#endif
        }
    }

    //-----------------------------------------------------------------------------

private:
    AVLNode* RemoveNode(AVLNode* node, AVLNode* parent = nullptr) noexcept
    {
        if (not node) { 
            m_info.heightHasChanged = false; 
            return nullptr; 
        }

        const int rel = m_info.compareNodes(m_info.context, m_info.workingKey, node->key);

        if (rel < 0) {
            node->left = RemoveNode(node->left, node);
            if (m_info.heightHasChanged) node = BalanceLeftShrink(node);
            return node;
        }

        if (rel > 0) {
            node->right = RemoveNode(node->right, node);
            if (m_info.heightHasChanged) node = BalanceRightShrink(node);
            return node;
        }

        // Treffer
        m_info.result = true;
        m_info.workingParent = parent;
        m_info.workingNode = node;
        m_info.workingData = std::move(node->data);

#if RELINK_DELETED_NODE
        if (not node->right) {
            m_info.heightHasChanged = true;
            AVLNode* next = node->left;
            DeleteNode(node);
            return next;
        }

        if (not node->left) {
            m_info.heightHasChanged = true;
            AVLNode* next = node->right;
            DeleteNode(node);
            return next;
        }

        // zwei Kinder: Vorgänger aus linkem Teilbaum herauslösen und relinken
        node = UnlinkNode(node->left, node);          // liefert neue Wurzel an dieser Stelle
        if (m_info.heightHasChanged) 
            node = BalanceLeftShrink(node);
        DeleteNode(m_info.workingNode);               // ursprünglichen Knoten entfernen
        return node;
#else
        if (not node->right) {
            m_info.heightHasChanged = true;
            AVLNode* next = node->left;
            DeleteNode(node);
            return next;
        }

        if (not node->left) {
            m_info.heightHasChanged = true;
            AVLNode* next = node->right;
            DeleteNode(node);
            return next;
        }

        // zwei Kinder: Schlüssel/Daten mit Vorgänger tauschen, dann Vorgänger löschen
        node->left = UnlinkNode(node->left);
        if (m_info.heightHasChanged) 
            node = BalanceLeftShrink(node);
        DeleteNode(m_info.workingNode);               // der im Unlink gelöste Vorgänger
        return node;
#endif
    }

    //-----------------------------------------------------------------------------

public:
    template<typename K = KEY_T>
    bool Remove(K&& key)
    {
        if (not m_info.root or not m_info.compareNodes)
            return false;
        m_info.workingKey = std::forward<K>(key);
        m_info.workingNode = nullptr;
        m_info.heightHasChanged = false;
        m_info.result = false;
        m_info.root = RemoveNode(m_info.root);
#if AVL_DEBUG
        if (not CheckForCycles(m_info.root, true)) {}
#endif
        return m_info.result;
    }

    //-----------------------------------------------------------------------------

private:
    void DestroyNodes(AVLNode*& root) noexcept
    {
        if (root) {
            DestroyNodes(root->left);
            DestroyNodes(root->right);
            DeleteNode(root);
        }
    }

    //-----------------------------------------------------------------------------

public:
    void Destroy(void) noexcept
    {
        DestroyNodes(m_info.root);
    }

    //-----------------------------------------------------------------------------

private:
    bool WalkNodes(AVLNode* root)
    {
        if (root) {
            if (root->visited == m_info.visited)
                return false;
            root->visited = m_info.visited;
            if (not WalkNodes(root->left))
                return false;
            if (not m_info.processNode(root->key, &root->data))
                return false;
            if (not WalkNodes(root->right))
                return false;
        }
        return true;
    }

    //-----------------------------------------------------------------------------

public:
    template <class Context>
    bool Walk(bool (Context::* processor)(const KEY_T&, DATA_T*), Context* context)
    {
        m_info.processNode = [processor, context](const KEY_T& key, DATA_T* data) { return (context->*processor)(key, data); };
        m_info.visited++;
        return WalkNodes(m_info.root);
    }

    //-----------------------------------------------------------------------------

public:
    DATA_T* Min(void)
    {
        if (not m_info.root)
            return nullptr;
        AVLNode* p = m_info.root;
        for (; p->left; p = p->left)
            ;
        return &p->data;
    }

    //-----------------------------------------------------------------------------

public:
    DATA_T* Max(void)
    {
        if (not m_info.root)
            return nullptr;
        AVLNode* p = m_info.root;
        for (; p->right; p = p->right)
            ;
        return &p->data;
    }

    //-----------------------------------------------------------------------------

private:
    void ExtractMin(AVLNode*& root, bool& heightHasChanged)
    {
        AVLNode* r = root;

        if (not r)
            heightHasChanged = false;
        else if (r->left) {
            ExtractMin(r->left, heightHasChanged);
            if (heightHasChanged)
                r = BalanceLeftShrink(r);
        }
        else {
            AVLNode* d = r;
            m_info.workingData = std::move(r->data);
            r = nullptr;
            DeleteNode(d);
            heightHasChanged = true;
        }
        root = r;
    }

    //-----------------------------------------------------------------------------

public:
    bool ExtractMin(DATA_T& data)
    {
        if (not m_info.root)
            return false;
        bool height = false;
        ExtractMin(m_info.root, height);
        data = std::move(m_info.workingData);
        return true;
    }

    //-----------------------------------------------------------------------------

private:
    void ExtractMax(AVLNode*& root, bool& heightHasChanged)
    {
        AVLNode* r = root;

        if (not r)
            heightHasChanged = false;
        else if (r->right) {
            ExtractMax(r->right, heightHasChanged);
            if (heightHasChanged)
                r = BalanceRightShrink(r);
        }
        else {
            AVLNode* d = r;
            m_info.workingData = std::move(r->data);
            r = nullptr;
            DeleteNode(d);
            heightHasChanged = true;
        }
        root = r;
    }

    //-----------------------------------------------------------------------------

public:
    bool ExtractMax(DATA_T& data)
    {
        if (not m_info.root)
            return false;
        bool height = false;
        ExtractMax(m_info.root, height);
        data = std::move(m_info.workingData);
        return true;
    }

    //-----------------------------------------------------------------------------

public:
    bool Update(KEY_T oldKey, KEY_T newKey)
    {
        if (not Remove(oldKey))
            return false;
        if (not Insert(newKey, m_info.workingData))
            return false;
        return true;
    }

    //-----------------------------------------------------------------------------

public:
    template<typename K = KEY_T>
    inline DATA_T& operator[] (K&& key)
    {
        DATA_T* p = Find(std::forward<K>(key));
        return p ? *p : throw std::invalid_argument("not found");
    }

    //-----------------------------------------------------------------------------

public:
    inline AVLTree& operator= (std::initializer_list<std::pair<KEY_T, DATA_T>> data)
    {
        for (auto& d : data)
            Insert(d.first, d.second);
        return *this;
    }

    //-----------------------------------------------------------------------------

private:
    static bool CopyData(void* context, const KEY_T& key, DATA_T* data) {
        return static_cast<AVLTree*>(context)->Insert(key, *data);
    }

    //-----------------------------------------------------------------------------

public:
    AVLTree(AVLTree& other) {
        Copy(other);
    }

    AVLTree& operator=(const AVLTree& other) {
        Destroy();
        Copy(other);
        return *this;
    }

    AVLTree& operator+=(const AVLTree& other) {
        Copy(other);
        return *this;
    }

    inline AVLTree& Copy(AVLTree& other)
    {
        Walk(CopyData, this);
        return *this;
    }
};

// =================================================================================================
