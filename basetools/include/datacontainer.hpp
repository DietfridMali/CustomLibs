#pragma once

#include "list.hpp"
#include "array.hpp"

// =================================================================================================

template<typename DATA_T>
class DataContainer {
public:
    using DataList = List<DATA_T>;
    using DataTable = ManagedArray<DATA_T>;

    DataList    m_dataList;
    DataTable   m_dataTable;

    DataContainer()
        : m_dataList(), m_dataTable()
    {
    }

    inline DataList& GetList(void) {
        return m_dataList;
    }

    inline DataTable& GetTable(void) {
        return m_dataTable;
    }

    inline DATA_T& Append(DATA_T data) {
        return *m_dataList.Append(data);
    }

    inline DATA_T& operator[](const int i) {
        return m_dataTable[i];
    }

    inline const DATA_T& operator[](const int i) const {
        return m_dataTable[i];
    }

    inline int Count(void) {
        return static_cast<int>(m_dataList.IsEmpty() ? m_dataTable.Length() : m_dataList.Length());
    }

    inline auto begin() { return m_dataTable.begin(); }

    inline auto end() { return m_dataTable.end(); }

    inline auto rbegin() { return m_dataTable.rbegin(); }

    inline auto rend() { return m_dataTable.rend(); }

    inline auto begin() const { return m_dataTable.begin(); }

    inline auto end() const { return m_dataTable.end(); }

    inline auto rbegin() const { return m_dataTable.rbegin(); }

    inline auto rend() const { return m_dataTable.rend(); }

    void Convert(void) {
        m_dataTable.Reserve(m_dataList.Length());
        for (const auto& data : m_dataList)
            m_dataTable.Append(data);
        m_dataList.Clear();
    }

    void Destroy(void) {
        m_dataList.Clear();
        for (int i = 0; i < Count(); i++)
            delete m_dataTable[i];
        m_dataTable.Reset();
    }

    ~DataContainer(void) {
        Destroy();
    }
};

// =================================================================================================


