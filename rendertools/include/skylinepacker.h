#pragma once

#include <vector>

// =================================================================================================
// Rectangle packing, geometry only - no texture, no render target, nothing that costs the GPU
// anything. That separation is the point: a caller that wants to try a packing (a different order of
// the same rectangles, say, to see whether it needs fewer pages) can run thousands of packings
// without allocating a single texture, and the packing it finally keeps is produced by exactly the
// same code as the ones it tried.
//
// SKYLINE: the top contour of everything already placed, kept as horizontal segments. A new
// rectangle goes where its TOP edge ends up lowest; ties go to the narrower segment, because a wide
// gap is worth more to a wide rectangle than to this one. Unlike shelf packing this does not waste
// the difference between a row's height and its shorter rectangles - a row opened for a tall
// rectangle no longer keeps short ones from using the space beside it.
//
// Rectangles should be offered LARGEST FIRST; the packer does not sort, so the caller keeps its own
// order and the index it gets back. A rectangle that does not fit leaves the contour untouched, so
// the same one can be offered to the next page.

class SkylinePacker {
public:
	struct Place {
		int	x{ 0 };
		int	y{ 0 };
	};

	// Empties the contour and sets the page size. Everything else is meaningless before this.
	void Reset(int width, int height) {
		m_width = width;
		m_height = height;
		m_skyline.clear();
		if ((width > 0) and (height > 0)) {
			Node node;
			node.x = 0;
			node.y = 0;
			node.width = width;
			m_skyline.push_back(node);
		}
	}

	// Places the rectangle and fills in where it went. false = it does not fit anywhere on this page,
	// and the contour is unchanged.
	bool Add(int width, int height, Place& place) {
		if ((width <= 0) or (height <= 0) or (width > m_width) or (height > m_height))
			return false;

		int bestIndex = -1;
		int bestX = 0;
		int bestY = 0;
		int bestTop = m_height + 1;
		int bestWidth = m_width + 1;

		for (size_t i = 0; i < m_skyline.size(); i++) {
			int y = Fit(i, width, height);

			if (y < 0)
				continue;
			if ((y + height < bestTop) or ((y + height == bestTop) and (m_skyline[i].width < bestWidth))) {
				bestTop = y + height;
				bestWidth = m_skyline[i].width;
				bestIndex = int(i);
				bestX = m_skyline[i].x;
				bestY = y;
			}
		}
		if (bestIndex < 0)
			return false;
		Insert(size_t(bestIndex), bestX, bestY, width, height);
		place.x = bestX;
		place.y = bestY;
		return true;
	}

	inline int Width(void) const noexcept {
		return m_width;
	}

	inline int Height(void) const noexcept {
		return m_height;
	}

private:
	// One horizontal piece of the contour: everything below (x .. x + width, y) is taken.
	struct Node {
		int	x{ 0 };
		int	y{ 0 };
		int	width{ 0 };
	};

	std::vector<Node>	m_skyline;
	int					m_width{ 0 };
	int					m_height{ 0 };

	// Lowest edge at which a rectangle of this size fits with its left edge at node index, or -1 when
	// it does not fit there. Const on purpose: asking must not change the contour.
	int Fit(size_t index, int width, int height) const {
		if (m_skyline[index].x + width > m_width)
			return -1;

		// The rectangle sits on top of every segment it spans, so its bottom edge is the highest one.
		int y = m_skyline[index].y;
		int left = width;

		for (size_t i = index; left > 0; i++) {
			if (i >= m_skyline.size())
				return -1;
			if (y < m_skyline[i].y)
				y = m_skyline[i].y;
			if (y + height > m_height)
				return -1;
			left -= m_skyline[i].width;
		}
		return y;
	}

	void Insert(size_t index, int x, int y, int width, int height) {
		Node node;

		node.x = x;
		node.y = y + height;
		node.width = width;
		m_skyline.insert(m_skyline.begin() + index, node);

		// Whatever the new segment covers is shortened, and what it covers completely goes.
		for (size_t i = index + 1; i < m_skyline.size(); ) {
			if (m_skyline[i].x >= x + width)
				break;

			int overlap = x + width - m_skyline[i].x;

			if (overlap < m_skyline[i].width) {
				m_skyline[i].x += overlap;
				m_skyline[i].width -= overlap;
				break;
			}
			m_skyline.erase(m_skyline.begin() + i);
		}
		// Neighbours of equal height are one segment. Without this the list grows with every rectangle
		// and the search over it gets slower and slower.
		for (size_t i = 0; i + 1 < m_skyline.size(); ) {
			if (m_skyline[i].y == m_skyline[i + 1].y) {
				m_skyline[i].width += m_skyline[i + 1].width;
				m_skyline.erase(m_skyline.begin() + i + 1);
			}
			else
				i++;
		}
	}
};
