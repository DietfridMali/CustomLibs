#pragma once

#include <stdexcept>
#include "string.h"

    template < class KEY_T, class DATA_T >
    class AvlTree {
	    public:

            typedef int(*tComparator) (KEY_T const &, KEY_T const &);

            //----------------------------------------

		    class CNode;

		    typedef CNode* tNodePtr;

		    class CNode {
			    public:
				    KEY_T		key;
				    DATA_T		data;
				    tNodePtr	left;
				    tNodePtr	right;
				    char		balance;

				    CNode() : left(nullptr), right(nullptr), balance(0) {}

				    CNode(KEY_T key, DATA_T data) : key (key), data (data), left(nullptr), right(nullptr), balance(0) {}
			    };

		    //----------------------------------------

		    struct tAvlInfo {
			    tNodePtr	root;
			    tNodePtr	current;
			    KEY_T	    key;
                tComparator compare;
			    bool	    isDuplicate;
			    bool        heightHasChanged;

                tAvlInfo () : root (nullptr), current (nullptr), compare (nullptr), isDuplicate (false), heightHasChanged (false) {}
            };

		    typedef bool (* tNodeProcessor) (DATA_T const &);

	    private:

		    tAvlInfo	m_info;


		//----------------------------------------

	public:

		AvlTree () {
            memset (&m_info, 0, sizeof (m_info));
        }

		~AvlTree () { 
			Delete (); 
		}

        inline void SetComparator(tComparator compare) {
            m_info.compare = compare;
        }

//-----------------------------------------------------------------------------

#define AVL_OVERFLOW   1
#define AVL_BALANCED   0
#define AVL_UNDERFLOW  3

///-----------------------------------------------------------------------------

public:
DATA_T* Find(KEY_T key)
{
    for (tNodePtr node = m_info.root; node; ) {
        switch (m_info.compare(key, node->key)) {
            case -1:
                node = node->left;
                break;
            case 1:
                node = node->right;
                break;
            default:
                m_info.current = node;
                return &node->data;
        }
    }
    return nullptr;
}

//-----------------------------------------------------------------------------

private:    
tNodePtr AllocNode(void)
{
    if (m_info.current = new CNode()) {
        m_info.heightHasChanged = true;
        return m_info.current;
    }
    return nullptr;
}

//-----------------------------------------------------------------------------

public:
bool InsertNode(tNodePtr& root)
{
    tNodePtr node = root;
    if (!node)
        return ((root = AllocNode()) != nullptr);

    switch (m_info.compare (m_info.key, node->key)) {
        case -1:
            if (!InsertNode(node->left))
                return false;
            if (m_info.heightHasChanged) {
                switch (node->balance) {
                case AVL_UNDERFLOW:
                    tNodePtr child = node->left;
                    tNodePtr pivot = child->right;
                    if (child->balance == AVL_UNDERFLOW) {  // single LL rotation
                        node->left = pivot;
                        child->right = node;
                        node->balance = AVL_BALANCED;
                        node = child;
                    }
                    else { // double LR rotation
                        tNodePtr child = node->left;
                        tNodePtr pivot = child->right;
                        child->right = pivot->left;
                        pivot->left = child;
                        node->left = pivot->right;
                        pivot->right = node;
                        char b = pivot->balance;
                        node->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
                        child->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
                        node = pivot;
                    }
                    node->balance = AVL_BALANCED;
                    m_info.heightHasChanged = false;
                    break;

                case AVL_BALANCED:
                    node->balance = AVL_UNDERFLOW;
                    break;

                case AVL_OVERFLOW:
                    node->balance = AVL_BALANCED;
                    m_info.heightHasChanged = false;
                    break;
                }
            }
            break;

        case 0:
            m_info.isDuplicate = true;
            m_info.current = node;
            break;

        case 1:
            if (!InsertNode(node->right))
                return false;
            if (m_info.heightHasChanged) {
                switch (node->balance) {
                case AVL_UNDERFLOW:
                    node->balance = AVL_BALANCED;
                    m_info.heightHasChanged = false;
                    break;

                case AVL_BALANCED:
                    node->balance = AVL_OVERFLOW;
                    break;

                case AVL_OVERFLOW:
                    tNodePtr child = node->right;
                    tNodePtr pivot = child->left;
                    if (child->balance == AVL_OVERFLOW) { // single RR rotation
                        node->right = pivot;
                        child->left = node;
                        node->balance = AVL_BALANCED;
                        node = child;
                    }
                    else { // double RL rotation
                        child->left = pivot->right;
                        pivot->right = child;
                        node->right = pivot->left;
                        pivot->left = node;
                        char b = pivot->balance;
                        node->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
                        child->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
                        node = pivot;
                    }
                    node->balance = AVL_BALANCED;
                    m_info.heightHasChanged = false;
                }
            }
        break;
    }
    root = node;
    return true;
}

//-----------------------------------------------------------------------------

public:
bool Insert(KEY_T key, DATA_T data)
{
    m_info.key = key;
    m_info.heightHasChanged = m_info.isDuplicate = false;
    if (!InsertNode(m_info.root))
        return false;
    if (!m_info.isDuplicate) {
        m_info.current->key = key;
        m_info.current->data = data;
    }
    return true;
}

//-----------------------------------------------------------------------------

private:
void BalanceLShrink(tNodePtr& root, bool& heightHasChanged)
{
    tNodePtr node = root;
    switch (node->balance) {
    case AVL_UNDERFLOW:
        node->balance = AVL_BALANCED;
        break;

    case AVL_BALANCED:
        node->balance = AVL_OVERFLOW;
        heightHasChanged = false;
        break;

    case AVL_OVERFLOW:
        tNodePtr child = node->right;
        tNodePtr pivot = child->left;
        char b = child->balance;
        if (b != AVL_UNDERFLOW) { // single RR rotation
            node->right = pivot;
            child->left = node;
            if (b)
                node->balance = child->balance = AVL_BALANCED;
            else {
                node->balance = AVL_OVERFLOW;
                child->balance = AVL_UNDERFLOW;
                heightHasChanged = false;
            }
            node = child;
        }
        else { // double RL rotation
            child->left = pivot->right;
            pivot->right = child;
            node->right = pivot->left;
            pivot->left = node;
            b = pivot->balance;
            node->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
            child->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
            node = pivot;
            node->balance = AVL_BALANCED;
        }
    }
    root = node;
}

//-----------------------------------------------------------------------------

private:
void BalanceRShrink(tNodePtr& root, bool& heightHasChanged)
{
    tNodePtr node = *root;
    switch (node->balance) {
        case AVL_OVERFLOW:
            node->balance = AVL_BALANCED;
            break;

        case AVL_BALANCED:
            node->balance = AVL_UNDERFLOW;
            heightHasChanged = false;
            break;

        case AVL_UNDERFLOW:
            tNodePtr child = node->left;
            char b = child->balance;
            if (b != AVL_OVERFLOW) { // single LL rotation
                node->left = child->right;
                child->right = node;
                if (b)
                    node->balance = child->balance = AVL_BALANCED;
                else {
                    node->balance = AVL_UNDERFLOW;
                    child->balance = AVL_OVERFLOW;
                    heightHasChanged = false;
                }
                node = child;
            }
            else { // double LR rotation
                tNodePtr pivot = child->right;
                child->right = pivot->left;
                pivot->left = child;
                node->left = pivot->right;
                pivot->right = node;
                b = pivot->balance;
                node->balance = (b == AVL_UNDERFLOW) ? AVL_OVERFLOW : AVL_BALANCED;
                child->balance = (b == AVL_OVERFLOW) ? AVL_UNDERFLOW : AVL_BALANCED;
                node = pivot;
                node->balance = AVL_BALANCED;
            }
    }
    root = node;
}

//-----------------------------------------------------------------------------
//Free sucht das groesste Element im linken Unterbaum des auszufuegenden  
// Elements. Es nimmt die Stelle des auszufuegenden Elements im Baum ein. Dazu
// werden die Datenpointer der beiden Baumknoten *root und *p vertauscht.     
// Danach kann inDelete *root geloescht werden. Seine Stelle im-Baum  
// wird vom Knoten root->left eingenommen. Welcher Knoten zu loeschen ist,    
// wird vonFree in p vermerkt.                                            

private:
void UnlinkNode(tNodePtr& root, tNodePtr& delNode, bool& heightHasChanged)
{
    tNodePtr r = root;
    if (r->right) {
        UnlinkNode(r->right, delNode, heightHasChanged);
        if (heightHasChanged)
            BalanceRShrink(r, heightHasChanged);
    }
    else {
        tNodePtr d = delNode;
        DATA_T h = r->data;
        r->data = d->data;
        d->data = h;
        d = r;
        r = r->left;
        heightHasChanged = true;
        delNode = d;
    }
    root = r;
}

//-----------------------------------------------------------------------------

private:
bool RemoveNode(tNodePtr& root, bool& heightHasChanged)
{
    if (!root)
        heightHasChanged = false;
    else {
        tNodePtr r = root;
        if (m_info.key < r->key) {
            if (!RemoveNode(r->left))
                return false;
            if (heightHasChanged)
                BalanceLShrink(r, heightHasChanged);
        }
        else if (m_info.key > r->key) {
            if (!RemoveNode(r->right))
                return false;
            if (heightHasChanged)
                BalanceRShrink(r, heightHasChanged);
        }
        else {
            tNodePtr d = r;
            if (!r->right) {
                r = r->left;
                heightHasChanged = true;
            }
            else if (!r->left) {
                r = r->right;
                heightHasChanged = true;
            }
            else {
                UnlinkNode(d->left, d, heightHasChanged);
                if (heightHasChanged)
                    BalanceLShrink(r, heightHasChanged);
            }
            m_info.data = d->data;
            delete d;
        }
        root = r;
    }
    return true;
}

//-----------------------------------------------------------------------------

public:
bool Remove(KEY_T key)
{
    if (!m_info.root)
        return false;
    m_info.key = key;
    m_info.heightHasChanged = false;
    return Delete(m_info.root, m_info.heightHasChanged);
}

//-----------------------------------------------------------------------------

private:
void DeleteNodes(tNodePtr& root)
{
    if (root) {
        DeleteNodes(root->left);
        DeleteNodes(root->right);
        delete root;
    }
}

//-----------------------------------------------------------------------------

public:
void Delete(void)
{
    DeleteNodes(m_info.root);
}

//-----------------------------------------------------------------------------

private:
bool WalkNodes(tNodePtr root, tNodeProcessor processNode)
{
    if (root) {
        if (!WalkNodes(root->left, processNode))
            return false;
        if (!processNode(root->data))
            return false;
        if (!WalkNodes(root->right, processNode))
            return false;
    }
    return true;
}

//-----------------------------------------------------------------------------

public:
bool Walk(tNodeProcessor processNode)
{
    return WalkNodes(m_info.root, processNode);
} /*AvlWalk*/

//-----------------------------------------------------------------------------

public:
DATA_T* Min(void)
{
    if (!m_info.root)
        return nullptr;
    tNodePtr p = m_info.root;
    for (; p->left; p = p->left)
        ;
    return &p->data;
}

//-----------------------------------------------------------------------------

public:
DATA_T* Max(void)
{
    if (!m_info.root)
        return nullptr;
    tNodePtr p = m_info.root;
    for (; p->right; p = p->right)
        ;
    return &p->data;
}

//-----------------------------------------------------------------------------

private:
void ExtractMin(tNodePtr& root, bool& heightHasChanged)
{
    tNodePtr r = root;

    if (!r)
        m_info.heightHasChanged = false;
    else if (r->left) {
        ExtractMin(r->left);
        if (heightHasChanged)
            AvlBalanceLShrink(r, heightHasChanged);
    }
    else {
        tNodePtr d = r;
        m_info.data = r->data;
        r = nullptr;
        delete d;
        heightHasChanged = true;
    }
    root = r;
}

//-----------------------------------------------------------------------------

public:
bool ExtractMin(DATA_T& data)
{
    if (!m_info.root)
        return false;
    m_info.heightHasChanged = false;
    ExtractMin(m_info.root, m_info.heightHasChanged);
    data = m_info.data;
    return true;
}

//-----------------------------------------------------------------------------

private:
void ExtractMax(tNodePtr& root, bool& heightHasChanged)
{
    tNodePtr r = root;

    if (!r)
        m_info.heightHasChanged = false;
    else if (r->right) {
        ExtractMax(r->right);
        if (heightHasChanged)
            AvlBalanceRShrink(r, heightHasChanged);
    }
    else {
        tNodePtr d = r;
        m_info.data = r->data;
        r = nullptr;
        delete d;
        heightHasChanged = true;
    }
    root = r;
}

//-----------------------------------------------------------------------------

public:
bool ExtractMax(DATA_T& data)
{
    if (!m_info.root)
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
    if (!Remove(oldKey))
        return false;
    if (!Insert(newKey, m_info.data))
        return false;
    return true;
}

//-----------------------------------------------------------------------------

public:
inline DATA_T& operator[] (KEY_T key)
{
    DATA_T* p = Find(key);
    return p ? *p : throw std::invalid_argument("not found");
}

//-----------------------------------------------------------------------------

public:
    inline AvlTree& operator= (std::initializer_list<std::pair<KEY_T, DATA_T>> data)
    {
        for (auto d : data)
            Insert(d.first, d.second);
        return *this;
    }

    //-----------------------------------------------------------------------------

};

