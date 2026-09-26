#pragma once

#include "array.hpp"
#include "rendertarget.h"
#include "texture.h"

// =================================================================================================

class KuwaharaFilter {
public:
    struct Params {
        int     radius{ 8 };
        float   sharpness{ 4.0f };
        float   minSigma{ 0.02f };
        float   anisotropy{ 1.0f };
        float   tensorSigma{ 2.0f };
        float   alphaThreshold{ 0.5f };
        bool    wrapU{ true };
        bool    wrapV{ true };
    };

    ~KuwaharaFilter() {
        Destroy();
    }

    bool Apply(Texture* texture, const Params& params);

    bool ApplyCube(Texture* cubemap, const Params& params);

    void Destroy(void);

private:
    struct Targets {
        int             width{ 0 };
        int             height{ 0 };
        int             margin{ 0 };
        RenderTarget*   filter{ nullptr };
        RenderTarget*   tensor{ nullptr };
    };

    AutoArray<Targets>  m_targets;

    Targets* GetTargets(int width, int height, int margin);

    Shader* SetupShader(const char* shaderId, int width, int height, bool wrapU, bool wrapV, const Params& params);

    void SetupCubeFace(Shader* shader, const Targets& targets, int face);

    bool RenderTensor(Targets& targets, Texture* source, const Params& params, int face);

    bool RenderFilter(Targets& targets, Texture* source, const Params& params, int face);

    bool Filter(Texture* texture, const Params& params, bool isCube);

    bool ReplaceTexture(Texture* texture, AutoArray<uint8_t>& pixels, int width, int height, int faceCount);
};

// =================================================================================================
