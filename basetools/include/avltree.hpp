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
#include "string.h"

#include "avltreetraits.h"
#include "type_helper.hpp"

#define RELINK_DELETED_NODE 0

#define AVL_DEBUG 0

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
        AVLNodePtr	    root;
        AVLNodePtr	    workingNode;
        AVLNodePtr	    workingParent;
        int             nodeCount;
        KEY_T	        workingKey;
        DATA_T          workingData;
        Comparator      compareNodes;
        std::function<bool(const KEY_T&, DATA_T*)>  processNode;
        void*           context;
        int             visited;
        bool	        isDuplicate;
        bool            heightHasChanged;
        bool            result;
#ifdef _DEBUG
        AVLNodePtr      testNode;
        KEY_T           nullKey;
        KEY_T           testKey;
#endif

        tAVLTreeInfo()
            : root(nullptr), workingNode(nullptr), workingParent(nullptr), nodeCount(0), compareNodes(nullptr), processNode(nullptr), context(nullptr), visited(0), isDuplicate(false), heightHasChanged(false), result(false)
        {
            InitializeAnyType(workingData);
            InitializeAnyType(workingKey);
        }
    };

private:
    tAVLTreeInfo	        m_info;
#if DEBUG_MALLOC
    BasicDataPool<AVLNode>  m_nodePool;
    bool                    m_useNodePool;
#endif

//----------------------------------------

public:

AVLTree(int capacity = 0)
#if DEBUG_MALLOC
    : m_nodePool(), m_useNodePool(capacity > 0)
#endif
{
    memset(&m_info, 0, sizeof(m_info));
#if DEBUG_MALLOC
    m_nodePool.Create(capacity);
#endif
}


~AVLTree() {
    Destroy();
}

inline void SetComparator(Comparator compareNodes, void* context = nullptr) {  // context: pointer to some class instance containing the compare function, if that is a class member
    m_info.compareNodes = compareNodes;
    m_info.context = context;
}


inline int Size(void) {
    return m_info.nodeCount;
}

//-----------------------------------------------------------------------------

public:
DATA_T* Find(const KEY_T& key)
{
    if (not m_info.root)
        return nullptr;
    for (AVLNodePtr node = m_info.root; node != nullptr; ) {
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

inline DATA_T* Find(KEY_T&& key) {
    return Find(static_cast<const KEY_T&>(key));
}

public:
    AVLTree<KEY_T, DATA_T>::AVLNodePtr FindData(const DATA_T& data, AVLNodePtr node = nullptr, bool start = true)
    {
        if (start) {
            node = m_info.root;
            m_info.workingNode = nullptr;
            ++m_info.visited;
        }
        if (not node)
            return nullptr;
        if (node) {
            if (node->visited == m_info.visited) { // cyclical reference
                return nullptr;
            }
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
AVLNodePtr AllocNode(void)
{
#if DEBUG_MALLOC
    int poolIndex = -1;
    m_info.workingNode = m_useNodePool ? m_nodePool.Claim(poolIndex) : new AVLNode();
    if (not m_info.workingNode)
        return nullptr;
    new (m_info.workingNode) AVLNode();
    if constexpr (std::is_same<DATA_T, int>::value) {
        m_info.workingNode->data = -1;
    }
    m_info.workingNode->poolIndex = poolIndex;
    m_info.workingNode->left =
    m_info.workingNode->right = nullptr;
    m_info.workingNode->balance = AVL_BALANCED;
#else
    m_info.workingNode = new AVLNode();
#endif
    m_info.workingNode->key = std::move(m_info.workingKey);
    m_info.heightHasChanged = true;
    ++m_info.nodeCount;
    return m_info.workingNode;
}

//-----------------------------------------------------------------------------

void DeleteNode(AVLNodePtr& node) {
#if DBG_MALLOC
    if (m_useNodePool)
        m_nodePool.Release(node->poolIndex);
    else
#endif
        delete node;
    node = nullptr;
    --m_info.nodeCount;
}

//-----------------------------------------------------------------------------

public:
bool CheckForNullKey(AVLNodePtr root, bool start = true) {
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

 
bool CheckForCycles(AVLNodePtr node = nullptr, bool start = true) {
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
    inline AVLNodePtr BalanceLeftGrowth(AVLNodePtr node)
    {
        m_info.heightHasChanged = false;
        return node->RotateRight(AVL_UNDERFLOW, true);
    }

//-----------------------------------------------------------------------------

private:
    inline AVLNodePtr BalanceRightGrowth(AVLNodePtr node)
    {
        m_info.heightHasChanged = false;
        return node->RotateRight(AVL_OVERFLOW, true);
    }

//-----------------------------------------------------------------------------

private:
AVLNodePtr InsertNode(AVLNodePtr node, AVLNodePtr parent = nullptr)
{
    if (not node) {
        if (not (m_info.workingNode = AllocNode()))
            return nullptr;
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
        m_info.heightHasChanged = false; // Doppelte Schlüssel werden ignoriert
    }
    return node;
}

//-----------------------------------------------------------------------------

public:
    template<typename KEY_T, typename DATA_T>
    bool Insert(KEY_T&& key, DATA_T&& data, bool updateData = false)
    {
        m_info.workingKey = std::forward<KEY_T>(key);
        m_info.heightHasChanged = false;
        m_info.isDuplicate = false;
        m_info.workingNode = nullptr;
        m_info.root = InsertNode(m_info.root);
        if (not m_info.workingNode)
            return false;
        if (not m_info.isDuplicate or updateData)
            m_info.workingNode->data = std::move(data);
        return true;
    }

    bool Insert2(const KEY_T& key, const DATA_T& data, const KEY_T& nullKey, bool updateData = false)
    {
        m_info.workingKey = std::move(key);
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
            m_info.workingNode->data = std::move(data);
        else if (updateData)
            m_info.workingNode->data = std::move(data);
        return true;
    }

//-----------------------------------------------------------------------------

private:
    AVLNodePtr BalanceLeftShrink(AVLNodePtr node)
    {
        switch (node->balance) {
            case AVL_UNDERFLOW:
                node->balance = AVL_BALANCED;
                return node;

            case AVL_BALANCED:
                node->balance = AVL_OVERFLOW;
                m_info.heightHasChanged = false;
                return node;

            //case AVL_OVERFLOW:
            default:
                return node->BalanceLeftShrink(m_info.heightHasChanged);
        }
    }

//-----------------------------------------------------------------------------

    private:
        AVLNodePtr BalanceRightShrink(AVLNodePtr node)
        {
            switch (node->balance) {
                case AVL_OVERFLOW:
                    node->balance = AVL_BALANCED;
                    return node;

                case AVL_BALANCED:
                    node->balance = AVL_UNDERFLOW;
                    m_info.heightHasChanged = false;
                    return node;

                //case AVL_UNDERFLOW:
                default:
                    return node->BalanceRightShrink(m_info.heightHasChanged);
            }
        }

//-----------------------------------------------------------------------------
// UnlinkNode relinks the node to be deleted and its replacement node in the 
// AVLTree so that they swap places and returns the left subtree of the node 
// to be deleted with that of the replacement node's left subtree

private:
#if RELINK_DELETED_NODE
    void SwapNodes(AVLNodePtr delParent, AVLNodePtr delNode, AVLNodePtr replParent, AVLNodePtr replNode) {
        delParent->SetChild(delNode, replNode);
        if (replParent == delNode) {
            replNode->right = delNode->right;
        }
        else {
            replNode->right = delNode->right;
            replParent->right = replNode->left;
            replNode->left = delNode->left;
        }
        replNode->balance = delNode->balance;
    }
#endif

//-----------------------------------------------------------------------------

    AVLNodePtr UnlinkNode(
        AVLNodePtr node
#if RELINK_DELETED_NODE
        , AVLNodePtr parent
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
            // workingNode points at the node to be deleted
            // node is its replacement node
            // swap their positions in the tree by relinking them
            m_info.heightHasChanged = true;
            m_info.result = true;
#if RELINK_DELETED_NODE
            SwapNodes(m_info.workingParent, m_info.workingNode, parent, node);
            if (parent != m_info.workingParent)
                parent = BalanceRightShrink(parent);
            return parent->right; // parent->right has already been set correctly by SwapNodes, so just return it from here
#else
            //m_info.workingKey = std::move(m_info.workingNode->key);
            //m_info.workingData = std::move(m_info.workingNode->data);
            std::swap(m_info.workingNode->key, node->key);
            std::swap(m_info.workingNode->data, node->data);
            m_info.workingNode = node;
#if AVL_DEBUG
            CheckForCycles(m_info.root, true);
#endif
            return node->left; // this unlinks the node to be deleted and makes it left subtree the left subtree of the node that replaces it
#endif
        }
    }

//-----------------------------------------------------------------------------

private:
    AVLNodePtr RemoveNode(AVLNodePtr node, AVLNodePtr parent = nullptr)
    {
        if (not node)
            m_info.heightHasChanged = false;
        else {
            int rel = m_info.compareNodes(m_info.context, m_info.workingKey, node->key);
            if (rel < 0) {
                if (not (node->left = RemoveNode(node->left, node)))
                    return node;
                if (m_info.heightHasChanged)
                    node = BalanceLeftShrink(node);
            }
            else if (rel > 0) {
                if (not (node->right = RemoveNode(node->right, node)))
                    return node;
                if (m_info.heightHasChanged)
                    node =  BalanceRightShrink(node);
            }
            else {
                m_info.workingParent = parent;
                m_info.workingNode = node; // node to be deleted
                m_info.workingData = std::move(node->data);
                if (not node->right) {
                    m_info.heightHasChanged = true;
                    node = node->left;
                }
                else if (not node->left) {
                    m_info.heightHasChanged = true;
                    node = node->right;
                }
                else {
#if RELINK_DELETED_NODE
                    node->left = UnlinkNode(node->left, node);
#else
                    node->left = UnlinkNode(node->left);
#endif
                    if (m_info.heightHasChanged)
                        node = BalanceLeftShrink(node);
                }
                DeleteNode(m_info.workingNode);
            }
        }
        return node;
    }

//-----------------------------------------------------------------------------

public:
    template<typename KEY_T>
    bool Remove(KEY_T&& key)
    {
        if (not m_info.root or not m_info.compareNodes)
            return false;
#if 0
        DATA_T* data = Find(std::forward<KEY_T>(key));
        if (not data)
            return false;
        m_info.workingData = std::move(*data);
        if constexpr (std::is_trivially_constructible<DATA_T>::value)
            *data = -1;
        return true;
#else
        m_info.workingKey = std::forward<KEY_T>(key);
        m_info.workingNode = nullptr;
        m_info.heightHasChanged = false;
        m_info.result = false;
        //AVLTree backup(*this);
        m_info.root = RemoveNode(m_info.root);
        if (m_info.result)
            return false;
#if AVL_DEBUG
        if (not CheckForCycles(m_info.root, true))
            ;//  backup.Remove(key);
#endif
        return true;
#endif
    }

//-----------------------------------------------------------------------------

private:
    void DestroyNodes(AVLNodePtr& root)
    {
        if (root) {
            DestroyNodes(root->left);
            DestroyNodes(root->right);
            DeleteNode(root);
        }
    }

//-----------------------------------------------------------------------------

public:
    void Destroy(void)
    {
        DestroyNodes(m_info.root);
    }

//-----------------------------------------------------------------------------

private:
    bool WalkNodes(AVLNodePtr root)
    {
        if (root) {
            if (root->visited == m_info.visited)
                return false; // circular reference
            root->visited = m_info.visited;
            if (not WalkNodes(root->left))
                return false;
            //if (not m_info.processNode(m_info.context, root->key, root->data))
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
    bool Walk(bool (Context::*processor)(const KEY_T&, DATA_T*), Context* context)
    {
        //m_info.processNode = processNode;
        //m_info.context = context;
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
        AVLNodePtr p = m_info.root;
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
        AVLNodePtr p = m_info.root;
        for (; p->right; p = p->right)
            ;
        return &p->data;
    }

//-----------------------------------------------------------------------------

private:
    void ExtractMin(AVLNodePtr& root, bool& heightHasChanged)
    {
        AVLNodePtr r = root;

        if (not r)
            m_info.heightHasChanged = false;
        else if (r->left) {
            ExtractMin(r->left);
            if (heightHasChanged)
                AvlBalanceLShrink(r, heightHasChanged);
        }
        else {
            AVLNodePtr d = r;
            m_info.data = r->data;
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
        m_info.heightHasChanged = false;
        ExtractMin(m_info.root, m_info.heightHasChanged);
        data = m_info.data;
        return true;
    }

//-----------------------------------------------------------------------------

private:
    void ExtractMax(AVLNodePtr& root, bool& heightHasChanged)
    {
        AVLNodePtr r = root;

        if (not r)
            m_info.heightHasChanged = false;
        else if (r->right) {
            ExtractMax(r->right);
            if (heightHasChanged)
                AvlBalanceRShrink(r, heightHasChanged);
        }
        else {
            AVLNodePtr d = r;
            m_info.data = r->data;
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
        m_info.heightHasChanged = false;
        ExtractMax(m_info.root, m_info.heightHasChanged);
        data = m_info.data;
        return true;
    }

//-----------------------------------------------------------------------------

public:
    bool Update(KEY_T oldKey, KEY_T newKey)
    {
        if (not Remove(oldKey))
            return false;
        if (not Insert(newKey, m_info.data))
            return false;
        return true;
    }

//-----------------------------------------------------------------------------

public:
    template<typename KEY_T>
    inline DATA_T& operator[] (KEY_T&& key)
    {
        DATA_T* p = Find(std::forward<KEY_T>(key));
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
    static bool CopyData(void* context, const KEY_T& key, const DATA_T& data) {
        return static_cast<AVLTree*>(context)->Insert(key, data);
    }

//-----------------------------------------------------------------------------

public:
    AVLTree(AVLTree& other) {
        Copy(other);
    }

    AVLTree& operator=(const AVLTree& other) {
        Destroy();
        Copy(other);
    }

    AVLTree& operator+=(const AVLTree& other) {
        Copy(other);
    }

    inline AVLTree& Copy(AVLTree& other)
    {
#if DEBUG_MALLOC
        m_nodePool = other.m_nodePool;
        m_useNodePool = other.m_useNodePool;
#endif
        Walk(CopyData, this);
        return *this;
    }

};

// =================================================================================================

