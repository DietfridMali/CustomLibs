#pragma once

// =================================================================================================

class TableDimensions {
protected:
    int m_cols, m_rows, m_size;

public:
    TableDimensions(int cols = 0, int rows = 0, float scale = 1) {
        Init (cols, rows, scale);
    }

    void Init (int cols = 0, int rows = 0, float scale = 1) noexcept {
        m_cols = int (cols * scale);
        m_rows = int (rows * scale);
        m_size = m_cols * m_rows;
    }

    TableDimensions(TableDimensions const& other) 
        : m_cols(other.m_cols), m_rows(other.m_rows), m_size(other.m_size) 
    { }

    TableDimensions& operator=(const TableDimensions& other) noexcept {
        m_cols = other.m_cols;
        m_rows = other.m_rows;
        m_size = other.m_size;
        return *this;
    }

    TableDimensions& operator=(const TableDimensions&& other) noexcept {
        m_cols = other.m_cols;
        m_rows = other.m_rows;
        m_size = other.m_size;
        return *this;
    }

    bool operator== (TableDimensions& other) noexcept {
        return (m_cols == other.m_cols) and (m_rows == other.m_rows);
    }

    inline int GetSize(void) const noexcept {
        return m_size;
    }

    inline int GetCols(void) const noexcept {
        return m_cols;
    }

    inline void SetCols(int cols, float scale = 1) noexcept {
        m_cols = int(cols * scale);
        m_size = m_cols * m_rows;
    }

    inline int GetRows(void) const noexcept {
        return m_rows;
    }

    inline void SetRows(int rows, float scale = 1) noexcept {
        m_rows = int(rows * scale);
        m_size = m_rows * m_cols;
    }

    inline bool IsEmpty(void) const noexcept {
        return m_size == 0;
    }

    inline bool IsValid(void) const noexcept {
        return m_size > 0;
    }
};

// =================================================================================================

