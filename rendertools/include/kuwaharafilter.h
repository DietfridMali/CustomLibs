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
        int     blurRadius{ 0 };
    };

    ~KuwaharaFilter() {
        Destroy();
    }

    bool Apply(Texture* texture, const Params& params);

    bool ApplyCube(Texture* cubemap, const Params& params);

    bool FilterToBuffer(Texture* source, int width, int height, float* dest, const Params& params);

    RenderTarget* FilterToTarget(Texture* source, int width, int height, GfxPixelFormat filterFormat, const Params& params, const Viewport* area = nullptr);

    bool Prepare(int width, int height, GfxPixelFormat filterFormat);

    static int SourceMargin(const Params& params);

    void Destroy(void);

private:
    struct Targets {
        int             width{ 0 };
        int             height{ 0 };
        int             margin{ 0 };
        GfxPixelFormat  filterFormat{ GfxPixelFormat::RGBA8_UNorm };
        RenderTarget*   filter{ nullptr };
        RenderTarget*   tensor{ nullptr };
    };

    AutoArray<Targets>  m_targets;

    Targets* GetTargets(int width, int height, int margin, GfxPixelFormat filterFormat);

    Shader* SetupShader(const char* shaderId, int width, int height, bool wrapU, bool wrapV, const Params& params);

    void SetupCubeFace(Shader* shader, const Targets& targets, int face);

    void SetPassStates(void);

    void SetPassArea(const Viewport* area, int margin, int width, int height);

    bool RenderTensor(Targets& targets, Texture* source, const Params& params, int face, const Viewport* area = nullptr);

    bool RenderFilter(Targets& targets, Texture* source, const Params& params, int face, const Viewport* area = nullptr);

    bool RenderBlur(Targets& targets, Texture* source, const Params& params, int face);

    bool RenderFaces(Targets& targets, Texture* texture, const Params& params, bool isCube, bool isBlur, AutoArray<uint8_t>& pixels);

    bool Filter(Texture* texture, const Params& params, bool isCube);

    bool ReplaceTexture(Texture* texture, AutoArray<uint8_t>& pixels, int width, int height, int faceCount);
};

// =================================================================================================
