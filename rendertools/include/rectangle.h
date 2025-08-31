#pragma once

// =================================================================================================

class Rectangle {
public:
    int     m_left;
    int     m_top;
    int     m_right;
    int     m_bottom;
    int     m_width;
    int     m_height;
    float   m_aspectRatio;

    Rectangle(int left = 0, int top = 0, int width = 0, int height = 0) noexcept {
        Define(left, top, width, height);
    }

    inline void Define(int left = 0, int top = 0, int width = 0, int height = 0) noexcept {
        m_left = left;
        m_top = top;
        m_right = m_left + width - 1;
        m_bottom = m_top + height - 1;
        m_width = width;
        m_height = height;
        m_aspectRatio = (height == 0) ? 0.0f : float(width) / float(height);
    }

    inline bool Contains(int x, int y) const noexcept {
        return (x >= m_left) and (x <= m_right) and (y >= m_top) and (y <= m_bottom);
    }

    auto Center(void) const noexcept {
        struct result { int x; int y; };
        return result{ m_left + m_width / 2, m_top + m_height / 2 };
    }

    inline int Left(void) const noexcept {
        return m_left;
    }

    inline int Right(void) const noexcept {
        return m_right;
    }

    inline int Top(void) const noexcept {
        return m_top;
    }

    inline int Bottom(void) const noexcept {
        return m_bottom;
    }

    inline int Width(void) const noexcept {
        return m_width;
    }

    inline int Height(void) const noexcept {
        return m_height;
    }

    bool operator==(const Rectangle& other) const noexcept {
        return m_left == other.m_left and m_top == other.m_top and m_width == other.m_width and m_height == other.m_height;
    }

    bool operator!=(const Rectangle& other) const noexcept {
        return m_left != other.m_left or m_top != other.m_top or m_width != other.m_width or m_height != other.m_height;
    }

    void Resize(int dx, int dy) {
        m_left += dx;
        m_top += dy;
        m_right -= dx;
        m_bottom -= dy;
        m_width -= 2 * dx;
        m_height -= 2 * dy;

    }
};

// =================================================================================================
