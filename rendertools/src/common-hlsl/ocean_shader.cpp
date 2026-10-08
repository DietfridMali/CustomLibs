#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

static const String& OceanConstants() {
    static const String source(R"(
        cbuffer ShaderConstants : register(b1) {
            float4 tileSizes;
            float  time;
            float  deltaTime;
            float  gravity;
            float  choppiness;
            float  foamDecay;
            int    gridSize;
            int    logSize;
            int    stage;
            int    isVertical;
            int    cascade;
        };

        float2 ComplexMul(float2 a, float2 b) {
            return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
        }
    )");
    return source;
}


static AutoArray<ComputeBindingDesc> MakeOceanBindings(int sampledCount, int storageImageCount, int storageBufferCount) {
    AutoArray<ComputeBindingDesc> b;
    b.SetAutoFit(true);
    using Kind = ComputeBindingDesc::Kind;
    *b.Append() = { .binding = 1, .kind = Kind::UniformBuffer, .count = 1 };
    for (int i = 0; i < sampledCount; ++i)
        *b.Append() = { .binding = uint32_t(4 + i), .kind = Kind::SampledImage, .count = 1 };
    for (int i = 0; i < storageImageCount; ++i)
        *b.Append() = { .binding = uint32_t(36 + i), .kind = Kind::StorageImage, .count = 1 };
    for (int i = 0; i < storageBufferCount; ++i)
        *b.Append() = { .binding = uint32_t(36 + storageImageCount + i), .kind = Kind::StorageBuffer, .count = 1 };
    return b;
}

// -------------------------------------------------------------------------------------------------

static const String OceanInitMain = String(R"(
    [[vk::image_format("rgba32f")]] RWTexture2D<float4> target0 : register(u0);
    RWStructuredBuffer<float4> spectrum : register(u1);

    [numthreads(8, 8, 1)]
    void CSMain(uint3 id : SV_DispatchThreadID) {
        uint width = uint(gridSize) * 3u;
        if ((id.x >= width) || (id.y >= uint(gridSize)))
            return;
        target0[id.xy] = spectrum[id.y * width + id.x];
    }
)");


const ShaderSource& OceanInitShader() {
    static AutoArray<ComputeBindingDesc> bindings = MakeOceanBindings(0, 1, 1);
    static const ShaderSource source(
        "oceanInit",
        {
            .cs = OceanConstants() + OceanInitMain,
            .computeBindings = bindings
        }
    );
    return source;
}

// -------------------------------------------------------------------------------------------------

static const String OceanSpectrumMain = String(R"(
    Texture2D<float4> source0 : register(t0);
    [[vk::image_format("rgba32f")]] RWTexture2D<float4> target0 : register(u0);
    [[vk::image_format("rgba32f")]] RWTexture2D<float4> target1 : register(u1);

    [numthreads(8, 8, 1)]
    void CSMain(uint3 id : SV_DispatchThreadID) {
        int size = gridSize;
        if ((id.x >= uint(size) * 3u) || (id.y >= uint(size)))
            return;
        int c = int(id.x) / size;
        int2 n = int2(int(id.x) - c * size, int(id.y));
        float tile = (c == 0) ? tileSizes.x : ((c == 1) ? tileSizes.y : tileSizes.z);
        float dk = 6.28318530718 / tile;
        float2 k = float2((n.x < size / 2) ? n.x : n.x - size, (n.y < size / 2) ? n.y : n.y - size) * dk;
        float kLen = max(length(k), 1.0e-6);

        float4 h0 = source0.Load(int3(id.xy, 0));
        float phase = sqrt(gravity * kLen) * time;
        float2 e = float2(cos(phase), sin(phase));
        float2 h = ComplexMul(h0.xy, e) + ComplexMul(float2(h0.z, -h0.w), float2(e.x, -e.y));
        float2 ih = float2(-h.y, h.x);

        float2 dx = ih * (k.x / kLen);
        float2 dz = ih * (k.y / kLen);
        float2 dxz = h * (-k.x * k.y / kLen);
        float2 sx = ih * k.x;
        float2 sz = ih * k.y;
        float2 dxx = h * (-k.x * k.x / kLen);
        float2 dzz = h * (-k.y * k.y / kLen);

        target0[id.xy] = float4(dx.x - dz.y, dx.y + dz.x, h.x - dxz.y, h.y + dxz.x);
        target1[id.xy] = float4(sx.x - sz.y, sx.y + sz.x, dxx.x - dzz.y, dxx.y + dzz.x);
    }
)");


const ShaderSource& OceanSpectrumShader() {
    static AutoArray<ComputeBindingDesc> bindings = MakeOceanBindings(1, 2, 0);
    static const ShaderSource source(
        "oceanSpectrum",
        {
            .cs = OceanConstants() + OceanSpectrumMain,
            .computeBindings = bindings
        }
    );
    return source;
}

// -------------------------------------------------------------------------------------------------

static const String OceanTransformMain = String(R"(
    Texture2D<float4> source0 : register(t0);
    Texture2D<float4> source1 : register(t1);
    [[vk::image_format("rgba32f")]] RWTexture2D<float4> target0 : register(u0);
    [[vk::image_format("rgba32f")]] RWTexture2D<float4> target1 : register(u1);

    [numthreads(8, 8, 1)]
    void CSMain(uint3 id : SV_DispatchThreadID) {
        int size = gridSize;
        if ((id.x >= uint(size) * 3u) || (id.y >= uint(size)))
            return;
        int c = int(id.x) / size;
        int2 p = int2(int(id.x) - c * size, int(id.y));
        int index = (isVertical != 0) ? p.y : p.x;
        int halfSpan = 1 << stage;
        int j = index & (halfSpan - 1);
        int a = (index & ~(2 * halfSpan - 1)) + j;
        int b = a + halfSpan;
        if (stage == 0) {
            a = int(reversebits(uint(a)) >> uint(32 - logSize));
            b = int(reversebits(uint(b)) >> uint(32 - logSize));
        }
        float angle = 3.14159265359 * float(j) / float(halfSpan);
        float2 w = float2(cos(angle), sin(angle));
        float direction = ((index & halfSpan) == 0) ? 1.0 : -1.0;

        int3 pa = (isVertical != 0) ? int3(int(id.x), a, 0) : int3(c * size + a, p.y, 0);
        int3 pb = (isVertical != 0) ? int3(int(id.x), b, 0) : int3(c * size + b, p.y, 0);

        float4 a0 = source0.Load(pa);
        float4 b0 = source0.Load(pb);
        float4 a1 = source1.Load(pa);
        float4 b1 = source1.Load(pb);

        target0[id.xy] = float4(a0.xy + direction * ComplexMul(w, b0.xy), a0.zw + direction * ComplexMul(w, b0.zw));
        target1[id.xy] = float4(a1.xy + direction * ComplexMul(w, b1.xy), a1.zw + direction * ComplexMul(w, b1.zw));
    }
)");


const ShaderSource& OceanTransformShader() {
    static AutoArray<ComputeBindingDesc> bindings = MakeOceanBindings(2, 2, 0);
    static const ShaderSource source(
        "oceanTransform",
        {
            .cs = OceanConstants() + OceanTransformMain,
            .computeBindings = bindings
        }
    );
    return source;
}

// -------------------------------------------------------------------------------------------------

static const String OceanAssembleMain = String(R"(
    Texture2D<float4> source0 : register(t0);
    Texture2D<float4> source1 : register(t1);
    Texture2D<float4> source2 : register(t2);
    [[vk::image_format("rgba16f")]] RWTexture2D<float4> target0 : register(u0);
    [[vk::image_format("rgba16f")]] RWTexture2D<float4> target1 : register(u1);

    [numthreads(8, 8, 1)]
    void CSMain(uint3 id : SV_DispatchThreadID) {
        int size = gridSize;
        if ((id.x >= uint(size)) || (id.y >= uint(size)))
            return;
        int3 p = int3(cascade * size + int(id.x), int(id.y), 0);
        float4 a = source0.Load(p);
        float4 b = source1.Load(p);
        float dxx = choppiness * b.z;
        float dzz = choppiness * b.w;
        float dxz = choppiness * a.w;
        float jacobian = (1.0 + dxx) * (1.0 + dzz) - dxz * dxz;
        float previous = source2.Load(int3(id.xy, 0)).a;
        float compression = max(1.0 - jacobian, previous - foamDecay * deltaTime / max(jacobian, 0.5));
        target0[id.xy] = float4(choppiness * a.x, a.z, choppiness * a.y, compression);
        target1[id.xy] = float4(b.x, b.y, dxx, dzz);
    }
)");


const ShaderSource& OceanAssembleShader() {
    static AutoArray<ComputeBindingDesc> bindings = MakeOceanBindings(3, 2, 0);
    static const ShaderSource source(
        "oceanAssemble",
        {
            .cs = OceanConstants() + OceanAssembleMain,
            .computeBindings = bindings
        }
    );
    return source;
}

// =================================================================================================
