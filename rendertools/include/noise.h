#pragma once

// =================================================================================================

namespace Noise
{
    inline int WrapInt(int v, int p) {
        int r = v % p;
        return (r < 0) ? r + p : r;
    }

    template<typename T>
    class GridPos {
    public:
        T x, y, z;

        GridPos() : x(0), y(0), z(0) {}
        GridPos(T X, T Y, T Z) : x(X), y(Y), z(Z) {}

        GridPos operator+(const GridPos& o) const {
            return GridPos(x + o.x, y + o.y, z + o.z);
        }
        GridPos operator-(const GridPos& o) const {
            return GridPos(x - o.x, y - o.y, z - o.z);
        }
        GridPos& operator+=(const GridPos& o) {
            x += o.x; y += o.y; z += o.z; return *this;
        }
        GridPos& operator-=(const GridPos& o) {
            x -= o.x; y -= o.y; z -= o.z; return *this;
        }
    };

    class GridPosi 
        : public GridPos<int> {
    public:
        using GridPos<int>::GridPos;

        void Wrap(int period) {
            if (period > 1) {
                x = WrapInt(x, period);
                y = WrapInt(y, period);
                z = WrapInt(z, period);
            }
        }
    };

    class GridPosf 
        : public GridPos<float> {
    public:
        using GridPos<float>::GridPos;

        void Wrap(int period) {
            if (period > 1) {
                auto wrap = [period](float v) {
                    float iv = floorf(v);
                    float fv = v - iv;
                    int wi = WrapInt((int)iv, period);
                    return (float)wi + fv;
                    };
                x = wrap(x);
                y = wrap(y);
                z = wrap(z);
            }
        }
    };

    inline float Dot(const GridPosf& a, const GridPosf& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // -------------------------------------------------------------------------------------------------
    
    float Perlin(float x, float y, float z); // 3D perlin noise

	float ImprovedNoise(float x, float y, float z, const std::vector<int>& perm, int period);

	void BuildPermutation(std::vector<int>& perm, int period, uint32_t seed);

	float SimplexPerlin(float x, float y, float z);

	float SimplexAshima(float x, float y, float z);

    float Worley(const GridPosf& p, int period);
};

// =================================================================================================

