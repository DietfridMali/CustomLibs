#pragma once

// =================================================================================================

namespace Noise
{
    inline int WrapInt(int v, int p) {
        int r = v % p;
        return (r < 0) ? r + p : r;
    }

    inline float Mod289 (float v) {
        return v - floorf(v * (1.f / 289.f)) * 289.f;
        };

    template<typename T>
    class GridPos {
    public:
        T x, y, z;

        GridPos() 
            : x(0), y(0), z(0) {}

        GridPos(T X, T Y, T Z) 
            : x(X), y(Y), z(Z) {}

        GridPos operator+(const GridPos& other) const {
            return GridPos(x + other.x, y + other.y, z + other.z);
        }
        GridPos operator-(const GridPos& other) const {
            return GridPos(x - other.x, y - other.y, z - other.z);
        }
        GridPos& operator+=(const GridPos& other) {
            x += other.x; y += other.y; z += other.z; 
            return *this;
        }
        GridPos& operator-=(const GridPos& other) {
            x -= other.x; y -= other.y; z -= other.z; 
            return *this;
        }

        GridPos& operator+=(T n) {
            x += n; y += n; z += n;
            return *this;
        }

        GridPos& operator-=(T n) {
            x -= n; y -= n; z -= n;
            return *this;
        }

        GridPos operator+(T n) const {
            return GridPos(x + n, y + n, z + n);
        }

        GridPos operator-(T n) const {
            return GridPos(x - n, y - n, z - n);
        }

        GridPos& operator*=(T n) {
            x *= n; y *= n; z *= n; 
            return *this; 
        }

        GridPos operator*(T n) const {
            return GridPos(x * n, y * n, z * n);
        }

        friend GridPos operator+(T n, const GridPos& p) {
            return GridPos(n + p.x, n + p.y, n + p.z);
        }

        friend GridPos operator-(T n, const GridPos& p) {
            return GridPos(n - p.x, n - p.y, n - p.z);
        }
    };

    class GridPosi 
        : public GridPos<int> {
    public:
        using GridPos<int>::GridPos;

        GridPosi(const GridPos<int>& p)
            : GridPos<int>(p) {}

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

        GridPosf(const GridPos<float>& p)
            : GridPos<float>(p) {}

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

        float Dot(const GridPosf& other) const {
            return x * other.x + y * other.y + z * other.z;
        }

        GridPosf& Mod289(void){
            x = Noise::Mod289(x);
            y = Noise::Mod289(y);
            z = Noise::Mod289(z);
            return *this;
        }
    };

    // -------------------------------------------------------------------------------------------------
    
    float Perlin(float x, float y, float z); // 3D perlin noise

	float ImprovedNoise(float x, float y, float z, const std::vector<int>& perm, int period);

	void BuildPermutation(std::vector<int>& perm, int period, uint32_t seed);

	float SimplexPerlin(float x, float y, float z);

	float SimplexAshima(float x, float y, float z);

    float Worley(const GridPosf& p, int period);
};

// =================================================================================================

