#pragma once


#include "FBM.h"
#include "colordata.h"

// =================================================================================================

enum class NoiseWarp {
    Infinite,
    Periodic,
    None
};

struct NoiseParams {
    uint32_t    seed{ 0x1234567u };
    int         cellsPerAxis{ 8 };
    int         normalize{ 1 };
    NoiseWarp   warping{ NoiseWarp::None };

    // 3D Cloud/Region/Detail-Noise via CloudNoise::Compute: per-FBM-channel params.
    // Defaults match the previously hard-coded behavior of CloudNoise::PerlinFBM / WorleyFBM,
    // so callers that don't override these get identical noise output to before.
    FBMParams   perlinParams {
        .frequency   = 4.0f,
        .lacunarity  = 2.0f,
        .initialGain = 1.0f,
        .gain        = 0.75f,
        .octaves     = 7,
    };
    // worleyParams.initialGain == 0 → hard-coded 3-octave path with weights 0.625/0.25/0.125.
    // Set initialGain != 0 to switch to the generic FBM-loop (geometric series).
    FBMParams   worleyParams {
        .frequency   = 4.0f,
        .lacunarity  = 2.0f,
        .initialGain = 0.0f,
        .gain        = 0.5f,
        .octaves     = 3,
    };
};

// -------------------------------------------------------------------------------------------------

namespace Noise
{
    struct grad3 { float x, y, z; };

    inline int WrapInt(int v, int p) {
        if (p < 2)
            return v;
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
            x += other.x; 
            y += other.y; 
            z += other.z; 
            return *this;
        }
        GridPos& operator-=(const GridPos& other) {
            x -= other.x; 
            y -= other.y; 
            z -= other.z; 
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

        T Sum(void) {
            return x + y + z;
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
        using GridPos<float>::operator+;   // wichtig
        using GridPos<float>::operator-;
        using GridPos<float>::operator+=;
        using GridPos<float>::operator-=;
        using GridPos<float>::operator*=;
        using GridPos<float>::operator*;

        GridPosf(float x, float y, float z)
            : GridPos<float>(x, y, z) 
        { }

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

        GridPosf& operator+=(const GridPosi& other) {
            x += float(other.x);
            y += float(other.y);
            z += float(other.z);
            return *this;
        }

        GridPosf operator+(const GridPosi& other) const {
            return GridPosf(x + float(other.x), y + float(other.y), z + float(other.z));
        }

        GridPosf& operator-=(const GridPosi& other) {
            x -= float(other.x);
            y -= float(other.y);
            z -= float(other.z);
            return *this;
        }

        GridPosf operator-(const GridPosi& other) const {
            return GridPosf(x - float(other.x), y - float(other.y), z - float(other.z));
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
    
	float SimplexPerlin(Vector3f p, uint32_t seed);

    float PeriodicSimplexPerlin(Vector3f p, int period, uint32_t seed);

	float SimplexAshima(Vector3f p);

    float PeriodicSimplexAshima(Vector3f p, int period);

    float SimplexAshimaGLSL(Vector3f p);

    float Worley(Vector3f p, int period, uint32_t seed);

    uint8_t Hash2iByte(int ix, int iy, uint32_t seed, uint32_t ch);

    uint32_t HashXYC32(int x, int y, uint32_t seed, uint32_t ch);

    class PerlinNoise {
    private:
        Vector3f m_p;
        int      m_period;
        uint32_t m_seed;

        grad3 Gradient(int ix, int iy, int iz);

        float GradientDot(int ix, int iy, int iz);

    public:
        void Setup(int period, uint32_t seed);

        float Compute(Vector3f p);
    };


    class ImprovedPerlinNoise {
    public:
        // Permutation-Tabelle hat FIXE Größe (period-unabhängig), damit auch bei kleinen
        // Tile-Perioden (z.B. period=4 für die erste FBM-Oktave) volle Gradient-Diversität
        // erhalten bleibt. Coord-Wrap auf m_period erfolgt separat für seamless tiling.
        static constexpr int PERM_SIZE = 256;
        static constexpr int PERM_MASK = PERM_SIZE - 1;   // for `& PERM_MASK` indexing

    private:
        Vector3f         m_p;
        int              m_period;
        uint32_t         m_seed;
        uint32_t         m_builtSeed{ 0 };                // seed of currently built m_perm; 0 = none
        std::vector<int> m_perm;                          // size = PERM_SIZE, regardless of period

        void BuildPermutation(void);

        grad3 Gradient(int x, int y, int z);

        float GradientDot(int ix, int iy, int iz);

    public:
        void Setup(int period, uint32_t seed = 0x9E3779B9u);

        float Compute(Vector3f& p);
    };

    class CloudNoise {
    private:
        PerlinNoise         m_perlin;
        ImprovedPerlinNoise m_improvedPerlin;
        uint32_t            m_perlinSeed;
        uint32_t            m_worleySeed;
        FBMParams           m_perlinParams;
        FBMParams           m_worleyParams;

    public:
        void Setup(int basePeriod, uint32_t perlinSeed, uint32_t worleySeed);

        // Plumb the per-FBM-channel parameters in before the per-voxel Compute loop.
        // Without an explicit call, the FBMParams default-constructors apply (which match
        // the previously hard-coded behavior, see comment in NoiseParams above).
        void SetFbmParams(const FBMParams& perlinParams, const FBMParams& worleyParams) {
            m_perlinParams = perlinParams;
            m_worleyParams = worleyParams;
        }

        RGBAColor Compute(Vector3f p);

        float Remap(float x, float oldMin, float oldMax, float newMin, float newMax);

    private:
        float WorleyFBM(Vector3f p, const FBMParams& params);

        float PerlinFBM(Vector3f p, const FBMParams& params);

        float WorleyNoise(const Vector3f& p, float freq);

        float GradientNoise(glm::vec3 x, float freq);

        glm::vec3 Hash33(glm::vec3 p);
    };

    // -------------------------------------------------------------------------------------------------

    struct PerlinFunctor {
        PerlinNoise generator;
        float operator()(Vector3f& p) {
            return generator.Compute(p); // ~[-1,1]
        }
    };

    struct ImprovedPerlinFunctor {
        ImprovedPerlinNoise generator;
        float operator()(Vector3f& p) {
            return generator.Compute(p); // ~[-1,1]
        }
    };

    struct SimplexPerlinFunctor {
        int period;
        uint32_t seed;
        float operator()(Vector3f& p) {
            return Noise::PeriodicSimplexPerlin(p, period, seed);
        }
    };

    struct SimplexAshimaFunctor {
        float operator()(Vector3f& p) {
            return Noise::SimplexAshima(p);
        }
    };

    struct SimplexAshimaGLSLFunctor {
        float operator()(Vector3f& p) {
            return Noise::SimplexAshimaGLSL(p);
        }
    };

    struct WorleyFunctor {
        int period;
        uint32_t seed;
        float operator()(Vector3f& p) {
            return Noise::Worley(p, period, seed);
        }
    };
};

// =================================================================================================

