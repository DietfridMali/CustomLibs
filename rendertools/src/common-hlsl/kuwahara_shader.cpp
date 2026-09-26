#include "array.hpp"
#include "string.hpp"
#include "base_shadercode.h"

// =================================================================================================

static const ShaderDataAttributes VtxTcAttrs[] = {
    { "Vertex",   0, ShaderDataAttributes::Float3 },
    { "TexCoord", 0, ShaderDataAttributes::Float2 },
};

// -------------------------------------------------------------------------------------------------

static const String& KuwaharaConstants() {
    static const String source(R"(
        cbuffer ShaderConstants : register(b1) {
            int    width;
            int    height;
            int    wrapU;
            int    wrapV;
            float  alphaThreshold;
    )");
    return source;
}


static const String& KuwaharaFuncs() {
    static const String source(R"(
        struct PSInput {
            float4 pos       : SV_Position;
            float3 fragPos   : TEXCOORD0;
            float2 fragCoord : TEXCOORD1;
        };

        int WrapTexel(int p, int size, int wrap) {
            if (wrap != 0)
                return ((p % size) + size) % size;
            return clamp(p, 0, size - 1);
        }

        int2 FilterTexel(int2 p) {
            return int2(WrapTexel(p.x, width, wrapU), WrapTexel(p.y, height, wrapV));
        }

        bool InsideFilter(int2 p) {
            bool insideU = (wrapU != 0) || ((p.x >= 0) && (p.x < width));
            bool insideV = (wrapV != 0) || ((p.y >= 0) && (p.y < height));
            return insideU && insideV;
        }

        bool Opaque(float4 c) {
            return c.a >= alphaThreshold;
        }
    )");
    return source;
}


static const String& KuwaharaPlaneSource() {
    static const String source(R"(
        float4 SourceTexel(int2 p) {
            return srcTex.Load(int3(FilterTexel(p), 0));
        }

        bool SourceInside(int2 p) {
            return InsideFilter(p);
        }

        int2 TensorOrigin(float4 pos) {
            return int2(pos.xy);
        }
    )");
    return source;
}


static const String& KuwaharaCubeConstants() {
    static const String source(R"(
            int    face;
            int    faceSize;
            int    margin;
    )");
    return source;
}


static const String& KuwaharaCubeSource() {
    static const String source(R"(
        float3 CubeDirection(int cubeFace, float2 uv) {
            if (cubeFace == 0)
                return float3(1.0, -uv.y, -uv.x);
            if (cubeFace == 1)
                return float3(-1.0, -uv.y, uv.x);
            if (cubeFace == 2)
                return float3(uv.x, 1.0, uv.y);
            if (cubeFace == 3)
                return float3(uv.x, -1.0, -uv.y);
            if (cubeFace == 4)
                return float3(uv.x, -uv.y, 1.0);
            return float3(-uv.x, -uv.y, -1.0);
        }

        void CubeFaceUV(float3 d, out int cubeFace, out float2 uv) {
            float3 a = abs(d);
            if ((a.x >= a.y) && (a.x >= a.z)) {
                cubeFace = (d.x > 0.0) ? 0 : 1;
                uv = ((d.x > 0.0) ? float2(-d.z, -d.y) : float2(d.z, -d.y)) / a.x;
            }
            else if (a.y >= a.z) {
                cubeFace = (d.y > 0.0) ? 2 : 3;
                uv = ((d.y > 0.0) ? float2(d.x, d.z) : float2(d.x, -d.z)) / a.y;
            }
            else {
                cubeFace = (d.z > 0.0) ? 4 : 5;
                uv = ((d.z > 0.0) ? float2(d.x, -d.y) : float2(-d.x, -d.y)) / a.z;
            }
        }

        float2 TexelCenterUV(int2 p) {
            return (float2(p) + 0.5) / float(faceSize) * 2.0 - 1.0;
        }

        float4 SourceTexel(int2 p) {
            int texelFace;
            float2 uv;
            CubeFaceUV(CubeDirection(face, TexelCenterUV(p)), texelFace, uv);
            int2 q = clamp(int2(floor((uv * 0.5 + 0.5) * float(faceSize))), int2(0, 0), int2(faceSize - 1, faceSize - 1));
            return srcTex.SampleLevel(s0, CubeDirection(texelFace, TexelCenterUV(q)), 0);
        }

        bool SourceInside(int2 p) {
            return true;
        }

        int2 TensorOrigin(float4 pos) {
            return int2(pos.xy) - int2(margin, margin);
        }
    )");
    return source;
}


static const String& KuwaharaTensorBody() {
    static const String source(R"(
        float3 SobelColor(int2 p, float3 center) {
            float4 c = SourceTexel(p);
            return Opaque(c) ? c.rgb : center;
        }

        float4 PSMain(PSInput i) : SV_Target {
            int2 p = TensorOrigin(i.pos);
            float3 c = SourceTexel(p).rgb;
            float3 bl = SobelColor(p + int2(-1, -1), c);
            float3 b = SobelColor(p + int2(0, -1), c);
            float3 br = SobelColor(p + int2(1, -1), c);
            float3 l = SobelColor(p + int2(-1, 0), c);
            float3 r = SobelColor(p + int2(1, 0), c);
            float3 tl = SobelColor(p + int2(-1, 1), c);
            float3 t = SobelColor(p + int2(0, 1), c);
            float3 tr = SobelColor(p + int2(1, 1), c);
            float3 gx = ((tr + 2.0 * r + br) - (tl + 2.0 * l + bl)) / 4.0;
            float3 gy = ((tl + 2.0 * t + tr) - (bl + 2.0 * b + br)) / 4.0;
            return float4(dot(gx, gx), dot(gy, gy), dot(gx, gy), 1.0);
        }
    )");
    return source;
}


static const String& KuwaharaFilterBody() {
    static const String source(R"(
        static const float sectorHalfAngle = 3.14159265 / 8.0;

        float4 PSMain(PSInput i) : SV_Target {
            int2 p = int2(i.pos.xy);
            float4 src = SourceTexel(p);
            if (!Opaque(src))
                return src;

            float3 tensor = TensorTexel(p);
            float e = tensor.x;
            float g = tensor.y;
            float f = tensor.z;
            float root = sqrt((e - g) * (e - g) + 4.0 * f * f);
            float lambda1 = 0.5 * (e + g + root);
            float lambda2 = 0.5 * (e + g - root);
            float2 v = float2(lambda1 - e, -f);
            float2 dir = (length(v) > 0.0) ? normalize(v) : float2(0.0, 1.0);
            float phi = -atan2(dir.y, dir.x);
            float localAnisotropy = (lambda1 + lambda2 > 0.0) ? (lambda1 - lambda2) / (lambda1 + lambda2) : 0.0;
            float a = radius * clamp((anisotropy + localAnisotropy) / anisotropy, 0.1, 2.0);
            float b = radius * clamp(anisotropy / (anisotropy + localAnisotropy), 0.1, 2.0);
            float cosPhi = cos(phi);
            float sinPhi = sin(phi);
            float2x2 sr = mul(float2x2(0.5 / a, 0.0, 0.0, 0.5 / b), float2x2(cosPhi, sinPhi, -sinPhi, cosPhi));
            int maxX = int(sqrt(a * a * cosPhi * cosPhi + b * b * sinPhi * sinPhi));
            int maxY = int(sqrt(a * a * sinPhi * sinPhi + b * b * cosPhi * cosPhi));
            float zeta = 2.0 / radius;
            float sinSector = sin(sectorHalfAngle);
            float eta = (zeta + cos(sectorHalfAngle)) / (sinSector * sinSector);
            float4 m[8];
            float3 s[8];

            for (int k = 0; k < 8; k++) {
                m[k] = float4(0.0, 0.0, 0.0, 0.0);
                s[k] = float3(0.0, 0.0, 0.0);
            }
            for (int j = -maxY; j <= maxY; j++) {
                for (int n = -maxX; n <= maxX; n++) {
                    float2 w0 = mul(sr, float2(float(n), float(j)));
                    if (dot(w0, w0) > 0.25)
                        continue;
                    int2 q = p + int2(n, j);
                    if (!SourceInside(q))
                        continue;
                    float4 c = SourceTexel(q);
                    if (!Opaque(c))
                        continue;
                    float w[8];
                    float sum = 0.0;
                    float2 u = w0;
                    float uxx = zeta - eta * u.x * u.x;
                    float uyy = zeta - eta * u.y * u.y;
                    float z = max(0.0, u.y + uxx);
                    w[0] = z * z;
                    sum += w[0];
                    z = max(0.0, -u.x + uyy);
                    w[2] = z * z;
                    sum += w[2];
                    z = max(0.0, -u.y + uxx);
                    w[4] = z * z;
                    sum += w[4];
                    z = max(0.0, u.x + uyy);
                    w[6] = z * z;
                    sum += w[6];
                    u = sqrt(2.0) / 2.0 * float2(u.x - u.y, u.x + u.y);
                    uxx = zeta - eta * u.x * u.x;
                    uyy = zeta - eta * u.y * u.y;
                    z = max(0.0, u.y + uxx);
                    w[1] = z * z;
                    sum += w[1];
                    z = max(0.0, -u.x + uyy);
                    w[3] = z * z;
                    sum += w[3];
                    z = max(0.0, -u.y + uxx);
                    w[5] = z * z;
                    sum += w[5];
                    z = max(0.0, u.x + uyy);
                    w[7] = z * z;
                    sum += w[7];
                    if (sum <= 0.0)
                        continue;
                    float gauss = exp(-3.125 * dot(u, u)) / sum;
                    float3 c2 = c.rgb * c.rgb;
                    for (int k2 = 0; k2 < 8; k2++) {
                        float wk = w[k2] * gauss;
                        m[k2] += float4(c.rgb * wk, wk);
                        s[k2] += c2 * wk;
                    }
                }
            }

            float3 result = float3(0.0, 0.0, 0.0);
            float weightSum = 0.0;
            for (int k3 = 0; k3 < 8; k3++) {
                if (m[k3].w <= 0.0)
                    continue;
                float3 mean = m[k3].rgb / m[k3].w;
                float variance = max(dot(s[k3] / m[k3].w - mean * mean, float3(1.0, 1.0, 1.0)), 0.0);
                float wk = pow(max(sqrt(variance), minSigma), -sharpness);
                result += wk * mean;
                weightSum += wk;
            }
            return (weightSum > 0.0) ? float4(result / weightSum, src.a) : src;
        }
    )");
    return source;
}

// -------------------------------------------------------------------------------------------------

const ShaderSource& KuwaharaTensorShader() {
    static const ShaderSource source(
        "kuwaharaTensor",
        Standard2DVS(),
        KuwaharaConstants() +
        String(R"(
        };
        Texture2D srcTex : register(t0);
        )") +
        KuwaharaFuncs() +
        KuwaharaPlaneSource() +
        KuwaharaTensorBody(),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// -------------------------------------------------------------------------------------------------

const ShaderSource& KuwaharaCubeTensorShader() {
    static const ShaderSource source(
        "kuwaharaCubeTensor",
        Standard2DVS(),
        KuwaharaConstants() +
        KuwaharaCubeConstants() +
        String(R"(
        };
        TextureCube  srcTex : register(t0);
        SamplerState s0     : register(s0);
        )") +
        KuwaharaFuncs() +
        KuwaharaCubeSource() +
        KuwaharaTensorBody(),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// -------------------------------------------------------------------------------------------------

const ShaderSource& KuwaharaTensorBlurShader() {
    static const ShaderSource source(
        "kuwaharaTensorBlur",
        Standard2DVS(),
        KuwaharaConstants() +
        String(R"(
            int    directionX;
            int    directionY;
            float  sigma;
        };
        Texture2D tensorTex : register(t0);
        )") +
        KuwaharaFuncs() +
        String(R"(
        float4 PSMain(PSInput i) : SV_Target {
            int2 p = int2(i.pos.xy);
            int2 direction = int2(directionX, directionY);
            int radius = int(ceil(3.0 * sigma));
            float twoSigma2 = 2.0 * sigma * sigma;
            float3 sum = float3(0.0, 0.0, 0.0);
            float weightSum = 0.0;
            for (int k = -radius; k <= radius; k++) {
                float w = exp(-float(k * k) / twoSigma2);
                sum += w * tensorTex.Load(int3(FilterTexel(p + k * direction), 0)).rgb;
                weightSum += w;
            }
            return float4(sum / weightSum, 1.0);
        }
        )"),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// -------------------------------------------------------------------------------------------------

const ShaderSource& KuwaharaFilterShader() {
    static const ShaderSource source(
        "kuwaharaFilter",
        Standard2DVS(),
        KuwaharaConstants() +
        String(R"(
            float  radius;
            float  anisotropy;
            float  sharpness;
            float  minSigma;
        };
        Texture2D srcTex    : register(t0);
        Texture2D tensorTex : register(t1);
        )") +
        KuwaharaFuncs() +
        KuwaharaPlaneSource() +
        String(R"(
        float3 TensorTexel(int2 p) {
            return tensorTex.Load(int3(p, 0)).rgb;
        }
        )") +
        KuwaharaFilterBody(),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// -------------------------------------------------------------------------------------------------

const ShaderSource& KuwaharaCubeFilterShader() {
    static const ShaderSource source(
        "kuwaharaCubeFilter",
        Standard2DVS(),
        KuwaharaConstants() +
        KuwaharaCubeConstants() +
        String(R"(
            float  radius;
            float  anisotropy;
            float  sharpness;
            float  minSigma;
        };
        TextureCube  srcTex    : register(t0);
        Texture2D    tensorTex : register(t1);
        SamplerState s0        : register(s0);
        )") +
        KuwaharaFuncs() +
        KuwaharaCubeSource() +
        String(R"(
        float3 TensorTexel(int2 p) {
            return tensorTex.Load(int3(p + int2(margin, margin), 0)).rgb;
        }
        )") +
        KuwaharaFilterBody(),
        ShaderDataLayout(VtxTcAttrs, 2)
    );
    return source;
}

// =================================================================================================
