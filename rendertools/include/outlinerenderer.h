#pragma once

#include "fbo.h"
#include "colordata.h"

// =================================================================================================

class OutlineRenderer {
public:
    struct AAMethod {
        String  method = "";
        int     strength = 0;
        inline bool ApplyAA() const { return method.Length() > 0; };
    };

    struct Decoration {
        float           outlineWidth = 0.0f;
        RGBAColor       outlineColor = ColorData::Invisible;
        struct AAMethod aaMethod = {};
        inline bool HaveOutline() const { return (outlineWidth > 0.0f) and (outlineColor.A() > 0.0f); };
        inline bool ApplyAA() const { return aaMethod.ApplyAA(); };
    };

    void AntiAlias(FBO* fbo, const AAMethod& aaMethod);

    void RenderOutline(FBO* fbo, const Decoration& decoration);
};

// =================================================================================================


