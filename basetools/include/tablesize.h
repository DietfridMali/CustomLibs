#pragma once

// =================================================================================================

class TableSize {
protected:
    int m_cols, m_rows, m_size;

public:
    TableSize(int cols = 0, int rows = 0, float scale = 1) {
        Init (cols, rows, scale);
    }

    void Init (int cols = 0, int rows = 0, float scale = 1) noexcept {
        m_cols = int (cols * scale);
        m_rows = int (rows * scale);
        m_size = m_cols * m_rows;
    }

    TableSize(TableSize const& other) 
        : m_cols(other.m_cols), m_rows(other.m_rows), m_size(other.m_size) 
    { }

    TableSize& operator=(const TableSize& other) noexcept {
        m_cols = other.m_cols;
        m_rows = other.m_rows;
        m_size = other.m_size;
        return *this;
    }

    TableSize& operator=(const TableSize&& other) noexcept {
        m_cols = other.m_cols;
        m_rows = other.m_rows;
        m_size = other.m_size;
        return *this;
    }

    bool operator== (TableSize& other) noexcept {
        return (m_cols == other.m_cols) and (m_rows == other.m_rows);
    }

    inline int GetSize(void) const noexcept {
        return m_size;
    }

    inline int GetCols(void) const noexcept {
        return m_cols;
    }

    inline int Width(void) noexcept {
        return m_cols;
    }

    inline void SetCols(int cols, float scale = 1) noexcept {
        m_cols = int(cols * scale);
        m_size = m_cols * m_rows;
    }

    inline int GetRows(void) const noexcept {
        return m_rows;
    }

    inline int Height(void) noexcept {
        return m_rows;
    }

    inline void SetRows(int rows, float scale = 1) noexcept {
        m_rows = int(rows * scale);
        m_size = m_rows * m_cols;
    }

    inline int Row(int i) noexcept {
        return i / m_cols;
    }

    inline int Col(int i) noexcept {
        return i % m_cols;
    }

    inline float Rowf(int i) noexcept {
        return float(Row(i));
    }

    inline float Colf(int i) noexcept {
        return float(Col(i));
    }

    inline bool IsEmpty(void) const noexcept {
        return m_size == 0;
    }

    inline bool IsValid(void) const noexcept {
        return m_size > 0;
    }
};

// =================================================================================================

