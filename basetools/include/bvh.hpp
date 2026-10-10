#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

#include "array.hpp"
#include "vector.hpp"

// =================================================================================================

class BVH {
public:
	struct Box {
		Vector3f vMin{ Vector3f::ZERO };
		Vector3f vMax{ Vector3f::ZERO };

		inline void Add(const Box& other)
		noexcept
		{
			vMin.Minimize(other.vMin);
			vMax.Maximize(other.vMax);
		}

		inline void Add(const Vector3f& point)
		noexcept
		{
			vMin.Minimize(point);
			vMax.Maximize(point);
		}

		inline float Area(void) const
		noexcept
		{
			Vector3f size = vMax - vMin;
			return 2.0f * (size.X() * size.Y() + size.Y() * size.Z() + size.Z() * size.X());
		}
	};

	struct Node {
		Box		box;
		int32_t	skip{ 0 };
		int32_t	first{ 0 };
		int32_t	count{ 0 };

		inline bool IsLeaf(void) const
		noexcept
		{
			return count > 0;
		}
	};

	static constexpr int32_t BinCount = 16;

	AutoArray<Node>		m_nodes;
	AutoArray<int32_t>	m_primitives;

	void Build(const AutoArray<Box>& boxes, int32_t leafSize = 1)
	{
		m_nodes.Clear();
		m_primitives.Resize(boxes.Length());
		for (int32_t i = 0; i < boxes.Length(); ++i)
			m_primitives[i] = i;
		if (boxes.IsEmpty())
			return;
		AutoArray<Vector3f> centers;
		centers.Resize(boxes.Length());
		for (int32_t i = 0; i < boxes.Length(); ++i)
			centers[i] = (boxes[i].vMin + boxes[i].vMax) * 0.5f;
		AutoArray<int32_t>	rightChildren;
		AutoArray<Task>		tasks;
		tasks.Append(Task{ 0, boxes.Length(), -1 });
		while (not tasks.IsEmpty()) {
			Task	task = tasks.Pop();
			int32_t	index = m_nodes.Length();
			if (task.parent >= 0)
				rightChildren[task.parent] = index;
			Node* node = m_nodes.Append();
			rightChildren.Append(-1);
			node->box = Bounds(boxes, task.first, task.count);
			int32_t leftCount = (task.count > leafSize) ? Split(boxes, centers, task.first, task.count) : 0;
			if (leftCount == 0) {
				node->first = task.first;
				node->count = task.count;
				continue;
			}
			tasks.Append(Task{ task.first + leftCount, task.count - leftCount, index });
			tasks.Append(Task{ task.first, leftCount, -1 });
		}
		m_nodes[0].skip = m_nodes.Length();
		for (int32_t i = 0; i < m_nodes.Length(); ++i) {
			if (m_nodes[i].IsLeaf())
				continue;
			m_nodes[i + 1].skip = rightChildren[i];
			m_nodes[rightChildren[i]].skip = m_nodes[i].skip;
		}
	}

	inline void Clear(void) {
		m_nodes.Clear();
		m_primitives.Clear();
	}

private:
	struct Task {
		int32_t first{ 0 };
		int32_t count{ 0 };
		int32_t parent{ -1 };
	};

	struct Bin {
		Box		box;
		int32_t	count{ 0 };

		inline void Add(const Box& other)
		noexcept
		{
			if (count == 0)
				box = other;
			else
				box.Add(other);
		}
	};

	static inline int32_t BinIndex(float value, float origin, float scale)
	noexcept
	{
		return (std::min)(int32_t((value - origin) * scale), BinCount - 1);
	}

	Box Bounds(const AutoArray<Box>& boxes, int32_t first, int32_t count) const
	{
		Box bounds = boxes[m_primitives[first]];
		for (int32_t i = first + 1, l = first + count; i < l; ++i)
			bounds.Add(boxes[m_primitives[i]]);
		return bounds;
	}

	int32_t Split(const AutoArray<Box>& boxes, const AutoArray<Vector3f>& centers, int32_t first, int32_t count)
	{
		Box centerBounds{ centers[m_primitives[first]], centers[m_primitives[first]] };
		for (int32_t i = first + 1, l = first + count; i < l; ++i)
			centerBounds.Add(centers[m_primitives[i]]);
		float	bestCost = (std::numeric_limits<float>::max)();
		int32_t	bestAxis = -1;
		int32_t	bestBin = 0;
		for (int32_t axis = 0; axis < 3; ++axis) {
			float origin = centerBounds.vMin[axis];
			float extent = centerBounds.vMax[axis] - origin;
			if (extent <= 0.0f)
				continue;
			float	scale = float(BinCount) / extent;
			Bin		bins[BinCount];
			for (int32_t i = first, l = first + count; i < l; ++i) {
				int32_t	primitive = m_primitives[i];
				Bin&	bin = bins[BinIndex(centers[primitive][axis], origin, scale)];
				bin.Add(boxes[primitive]);
				++bin.count;
			}
			float	rightAreas[BinCount];
			int32_t	rightCounts[BinCount];
			Bin		side;
			for (int32_t i = BinCount - 1; i > 0; --i) {
				if (bins[i].count > 0) {
					side.Add(bins[i].box);
					side.count += bins[i].count;
				}
				rightCounts[i] = side.count;
				rightAreas[i] = (side.count > 0) ? side.box.Area() : 0.0f;
			}
			side = Bin();
			for (int32_t i = 0; i < BinCount - 1; ++i) {
				if (bins[i].count > 0) {
					side.Add(bins[i].box);
					side.count += bins[i].count;
				}
				if ((side.count == 0) or (rightCounts[i + 1] == 0))
					continue;
				float cost = float(side.count) * side.box.Area() + float(rightCounts[i + 1]) * rightAreas[i + 1];
				if (cost < bestCost) {
					bestCost = cost;
					bestAxis = axis;
					bestBin = i;
				}
			}
		}
		if (bestAxis < 0)
			return count / 2;
		float		origin = centerBounds.vMin[bestAxis];
		float		scale = float(BinCount) / (centerBounds.vMax[bestAxis] - origin);
		int32_t*	begin = m_primitives.DataPtr(first);
		int32_t*	middle = std::partition(begin, begin + count, [&](int32_t primitive) {
			return BinIndex(centers[primitive][bestAxis], origin, scale) <= bestBin;
		});
		return int32_t(middle - begin);
	}
};

// =================================================================================================
