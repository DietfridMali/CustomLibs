#pragma once

// =================================================================================================

class Rectangle {
public:
    int m_left;
    int m_top;
    int m_right;
    int m_bottom;
    int m_width;
    int m_height;
    float m_aspectRatio;

    Rectangle(int left = 0, int top = 0, int width = 0, int height = 0) {
        Define(left, top, width, height);
    }

    inline void Define (int left = 0, int top = 0, int width = 0, int height = 0) {
        m_left = left;
        m_top = top;
        m_right = m_left + width - 1;
        m_bottom = m_top + height - 1;
        m_width = width;
        m_height = height;
        m_aspectRatio = (height == 0) ? 0 : float(width) / float(height);
    }

    inline bool Contains(int x, int y) {
        return (x >= m_left) and (x <= m_right) and (y >= m_top) and (y <= m_bottom);
    }

    auto Center(void) {
        struct result { int x; int y; };
        return result { m_left + m_width / 2, m_top + m_height / 2 };
    }

    inline int Left(void) {
        return m_left;
    }

    inline int Right(void) {
        return m_right;
    }

    inline int Top(void) {
        return m_top;
    }

    inline int Bottom(void) {
        return m_bottom;
    }

    inline int Width(void) {
        return m_width;
    }

    inline int Height(void) {
        return m_height;
    }

    bool operator==(Rectangle& other) {
        return m_left == other.m_left and m_top == other.m_top and m_width == other.m_width and m_height == other.m_height;
    }

    bool operator!=(Rectangle& other) {
        return m_left != other.m_left or m_top != other.m_top or m_width != other.m_width or m_height != other.m_height;
    }
};

// =================================================================================================

