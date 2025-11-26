// Copyright (c) 2025 Dietfrid Mali
// This software is licensed under the MIT License.
// See the LICENSE file for more details.

#pragma once

#include <list>
#include <functional>
#include <algorithm>
#include <utility>
#include <cstddef>
#include <cstring>
#include <stdexcept>

template<typename ITEM_T>
class List
{
    using ItemType = ITEM_T;
    using ItemFilter = std::function<bool(ItemType*)>;

private:
    std::list<ItemType> m_list;

    // ----------------------------------------------------------
    // Zentrale Iterator-Funktion: ElementAt
    // i == 0: return start
    // i == -1: return end
    // i < 0: return -i-th element counted from the end
    // ----------------------------------------------------------
    typename std::list<ItemType>::iterator ElementAt(int32_t i)
    {
        int32_t l = static_cast<int32_t>(m_list.size());
        if (i == 0)
            return m_list.begin();
        if (i < 0) {
            if (i == -1)
                return l ? std::prev(m_list.end()) : m_list.end();
            i = l + i;
        }
        if (i >= l)
            return m_list.end();
        typename std::list<ItemType>::iterator it;
        if (i <= l / 2) { // optimize iteration by starting at list end if i closer to that
            it = m_list.begin();
            std::advance(it, i);
        }
        else {
            it = m_list.end();
            std::advance(it, -static_cast<long>(l - i));
        }
        return it;
    }

public:
    List() = default;
    ~List() = default;

    List(std::initializer_list<ItemType> itemList) {
        for (const auto& elem : itemList)
            m_list.push_back(elem);
    }

    inline std::list<ItemType>& StdList(void) noexcept {
        return m_list;
    }

    inline operator std::list<ItemType>& () { return m_list; }

    inline operator const std::list<ItemType>& () const { return m_list; }

    List& operator=(std::initializer_list<ItemType> itemList) {
        m_list.clear();
        m_list.insert(m_list.end(), itemList.begin(), itemList.end());
        return *this;
    }

    // ----------------------------------------------------------

    inline ItemType& operator[](int32_t i)
    {
        auto it = ElementAt(i);
        if (it == m_list.end())
            throw std::out_of_range("List index out of range.");

        return *it;
    }

    // ----------------------------------------------------------

    template <typename T>
        requires std::constructible_from<ItemType, T&&>
    inline ItemType* Append(T&& dataItem) noexcept {
        try {
            m_list.push_back(std::forward<T>(dataItem));
        }
        catch (...) {
            return nullptr;
        }
        return &m_list.back();
    }

    ItemType* Append(void) noexcept {
        try {
            m_list.emplace_back();         // Default-Konstruktor von ItemType
        }
        catch (...) {
            return nullptr;
        }
        return &m_list.back();
    }


    template<typename... Args,
        typename = std::enable_if_t<(sizeof...(Args) > 0) &&
        std::is_constructible<ItemType, Args...>::value>>
        ItemType * Append(Args&&... args) noexcept(noexcept(m_list.emplace_back(std::forward<Args>(args)...)))
    {
        try {
            m_list.emplace_back(std::forward<Args>(args)...);
        }
        catch (...) {
            return nullptr;
        }
        return &m_list.back();
    }

    // ----------------------------------------------------------
 
    template<typename T>
        requires std::constructible_from<ItemType, T&&>
    ItemType* Insert(int32_t i, T&& dataItem)
    {
        if (i >= m_list.size())
        {
            m_list.push_back(std::forward<T>(dataItem));
            return &m_list.back();
        }
        auto it = ElementAt(i);
        it = m_list.insert(it, std::forward<T>(dataItem));
        return &(*it);
    }

    // ----------------------------------------------------------
    
    ItemType Extract(int32_t i)
    {
        auto it = ElementAt(i);
        if (it == m_list.end())
            return ItemType();
        ItemType value = *it;
        m_list.erase(it);
        return value;
    }


    bool Extract(ItemType& value, int32_t i)
    {
        auto it = ElementAt(i);
        if (it == m_list.end())
            return false;
        value = *it;
        m_list.erase(it);
        return true;
    }

    // ----------------------------------------------------------
    
    inline bool Discard(int32_t i)
    {
        auto it = ElementAt(i);
        if (it == m_list.end())
            return false;
        m_list.erase(it);
        return true;
    }


    inline auto Erase(int32_t i) {
        auto it = ElementAt(i);
        return (it == m_list.end()) ? it : m_list.Erase(it);
    }


    template<typename Iterator>
    Iterator Discard(Iterator it) { return m_list.erase(it); }

    inline void DiscardFirst(void) { m_list.pop_front(); }

    inline void DiscardLast(void) { m_list.pop_back(); }

    // ----------------------------------------------------------

    bool Remove(const ItemType& data)
    {
        auto it = std::find(m_list.begin(), m_list.end(), data);
        if (it != m_list.end())
            return false;
        m_list.erase(it);
        return true;
    }

    // ----------------------------------------------------------

    template<typename T>
    int Find(T&& data) {
        ItemType pattern = std::forward<T>(data);
        int i = 0;
        for (const auto& value : m_list) {
            if (value == pattern)
                return int(i);
            ++i;
        }
        return -1;
    }

    //-----------------------------------------------------------------------------

    template<typename FILTER_T>
    int32_t Filter(FILTER_T filter)
    {
        auto oldSize = m_list.size();
        if constexpr (std::is_pointer_v<ItemType>)
            // item ist ItemType (also z.B. Decal*), Ihr Lambda erwartet Decal*
            m_list.remove_if([&filter](ItemType item) { return filter(item); });
        else
            // item ist ItemType (z.B. Decal&), Ihr Lambda erwartet Decal&
            m_list.remove_if([&filter](ItemType& item) { return filter(item); });
        return static_cast<int32_t>(oldSize - m_list.size());
    }

    List& Move(List& other) noexcept {
        if (other.Length())
            m_list.splice(m_list.end(), other.m_list);
        return *this;
    }
    
    List& Move(List&& other) noexcept {
        if (other.Length())
            m_list.splice(m_list.end(), other.m_list); // 'other' ist L-Wert hier, splice erwartet lvalue-ref -> ok
        return *this;
    }

    // copy-append
    List& Copy(const List& other) {
        if (other.Length())
            m_list.insert(m_list.end(), other.m_list.begin(), other.m_list.end());
        return *this;
    }

    // operator+= Varianten
    List& operator+=(List& other) {            // move von lvalue-Quelle
        return Move(other);
    }

    List& operator=(List&& other) {           // move von rvalue-Quelle
        return Move(std::move(other));
    }

    List& operator=(const List& other) {      // optional: copy-append bei const
        return Copy(other);
    }

    List(const List& other) {
        Copy(other);
    }

    List(List&& other) noexcept {
        Move(other);
    }

    List& operator+=(List&& other) {           // move von rvalue-Quelle
        return Move(std::move(other));
    }

    List& operator+=(const List& other) {      // optional: copy-append bei const
        return Copy(other);
    }

    // Alias
    List& AppendList(const List& other) {
        return Copy(other);
    }

    // ----------------------------------------------------------

    inline int32_t Length(void) const { return static_cast<int32_t>(m_list.size()); }

    inline bool IsEmpty(void) const { return m_list.empty(); }

    inline void Clear(void) { m_list.clear(); }

    inline void Reset(void) { Clear(); }

    inline ItemType& First(void) {  return m_list.front(); }

    inline const ItemType& First(void) const { return m_list.front(); }

    inline ItemType& Last(void) { return m_list.back();  }

    inline const ItemType& Last(void) const { return m_list.back(); }

    inline auto begin() { return m_list.begin(); }
    
    inline auto end() { return m_list.end(); }
    
    inline auto begin() const { return m_list.begin(); }
    
    inline auto end() const { return m_list.end(); }

    inline const std::list<ItemType>& GetList() const { return m_list; }

    inline std::list<ItemType>& GetList() { return m_list; }

    template <typename T>
    void Push(T&& value) { m_list.push_back(std::forward<T>(value)); }

    ItemType Pop(void) {
        if (Length() == 0)
            return ItemType();
        ItemType value = m_list.back();
        m_list.pop_back();
        return value;
    }

    bool Pop(ItemType& value) {
        if (Length() == 0)
            return false;
        value = std::move(m_list.back());
        m_list.pop_back();
        return true;
    }
};
