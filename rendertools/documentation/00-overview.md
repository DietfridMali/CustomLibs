# rendertools - Overview

`rendertools` is a rendering library. It sits between an application's own renderer and a graphics
API, and it exists in three interchangeable builds: OpenGL, Vulkan and DirectX 12. An application is
written once, against the library's vocabulary, and picks a backend at build time.

Three applications use it today: *Paintjob Rampage* (Smiley Battle), *D2X-XL* and *SparTipp*.
Paintjob Rampage and D2X-XL have a project for each backend, SparTipp one for Vulkan and one for
OpenGL. None of them contains graphics API calls of its own; everything that talks to a driver lives
here.

## What belongs in the library, and what does not

The library owns everything that is the same regardless of what is being rendered:

- device, swap chain, command submission, resource lifetime
- which device features an application requires, and taking the device down and up again at run
  time
- render targets, viewports, render passes, draw buffer selection
- vertex/index/storage buffers, instance streams and indirect draws, meshes, and the layouts that
  describe them
- textures in all their shapes, their upload paths, samplers and mip chains
- shaders: source, compilation, caching, parameter binding
- VR: the OpenXR session, its layers and the multiview rendering of both eyes (Vulkan and DirectX)
- model loading (glTF/GLB and OBJ)
- ready-made drawing components (quad, cube, sphere, billboard, skybox, lines, lightning, text,
  shadow map and shadow atlas, noise, Kuwahara filter, FFT ocean) and a camera path spline

The application owns what is being rendered, in what order, and with which parameters. It derives
from the library's renderer class and adds its own passes; it does not reach past the library to the
API. The standing rule in these applications is that a missing feature is added *to* the library
rather than worked around next to it.

Below the library sits `basetools`, a separate library with the containers, string, math and file
types the code is written in (`AutoArray`, `List`, `String`, `Matrix4f`, `Vector3f`, ...). Those types
are kept free of rendering concerns; `rendertools` depends on them and not the other way round.

basetools also provides the two services the library reports through. `LogHandler` (`logHandler`) is
the one log: every message of rendertools goes there, none to `stderr` or `std::cout` directly.
`MissingFiles` (`missingFiles`) is where a loader reports a required file it could not load - a
texture, a font, a model or one of its images, the blue noise file - and the application decides what
follows by setting a handler. The third sibling library, `apptools` (arguments, crash handler,
platform, sound), is not used by rendertools.

## Directory layout

```
rendertools/
    include/            API-neutral headers
    src/                API-neutral implementation
        common-hlsl/    HLSL shader sources, compiled into the Vulkan and DirectX builds
    opengl/
        include/        OpenGL definitions of the per-backend headers
        src/            OpenGL implementation
        visualstudio/   project file producing the OpenGL build of the library, and its pch.h
        Makefile        the same build for Linux
    vulkan/             same, for Vulkan
    directx/            same, for DirectX 12
    documentation/      these documents
```

Not every shared file is part of every build: the OpenXR core (`base_xrhandler.cpp`), `OceanFFT`
and `PoseSpline` are compiled by the Vulkan and DirectX builds only.

The split is the central structural decision of the library and is described in
[01-layering-and-specialization.md](01-layering-and-specialization.md). In short: a name such as
`GfxRenderer`, `Shader`, `Texture`, `RenderTarget`, `GfxStates` exists once per backend, in a file of
the same name (`CommandList` only in the two backends that have command lists), and the build decides
which one is compiled. Shared code includes `"shader.h"` and gets the shader of whichever backend it
is being built for.

## Choosing a backend

Each backend has its own project file, and all three produce a library of the same name. They differ
in two things:

- the preprocessor symbol: `OPENGL`, `VULKAN` or `DIRECTX`
- the include path: shared `include/` plus the backend's own `include/`

Both are set in `opengl|vulkan|directx/visualstudio/rendertools.vcxproj`, and again in the `Makefile`
next to the `visualstudio` folder, which builds the same library on Linux. The Visual Studio include
path also starts with the shared `src/`. The Vulkan and DirectX builds additionally define
`GLM_FORCE_DEPTH_ZERO_TO_ONE`, because their clip space depth range is `[0,1]` where OpenGL's is
`[-1,1]`.

The builds also differ in the packages they compile against. Vulkan and DirectX use SDL 3 (SDL3,
SDL3_ttf, SDL3_image) and the OpenXR SDK; Vulkan adds the Vulkan SDK, VMA and SPIRV-Reflect. The
OpenGL project still points at SDL 2 and GLEW, while the shared sources it compiles - font handler,
text renderer, skybox, noise textures - and basetools' timer include SDL 3 headers. The applications
follow the same split: their Vulkan and DirectX projects use SDL 3, their OpenGL projects SDL 2.

A few further symbols switch diagnostics, not backends: `DBG_DIRECTX` (DirectX, on in Debug),
`VK_STALL_DIAG` (Vulkan stall recorder, off by default) and `LOG_FRAME_METRICS`
(`include/log_frame_metrics.h`, off unless the project sets it).

Every Visual Studio project builds with a precompiled header, `pch.h` in its `visualstudio` folder,
which the project force-includes into every source; no source names it. It holds only C++ standard
library headers and glm - nothing of the library's own and no windows, Vulkan, D3D12, GLEW or SDL
header, because some sources need control over what comes first (the Vulkan context defines
`VMA_IMPLEMENTATION` ahead of the Vulkan headers). The Makefiles build the same sources without it.

At runtime the library still knows which backend it is: `gfxapitype.h` defines `GfxApiType` and the
predicates `UsesOpenGL()`, `UsesVulkan()`, `UsesDirectX()`. This header is included *inside* the body
of `BaseRenderer`, so the enum and the predicates become members of the renderer and read as
`baseRenderer.UsesOpenGL()` at a call site. Each backend's `GfxRenderer` constructor sets the value.

The predicates are not a portability layer - they are for the handful of places where the difference
is real and cannot be hidden, above all the vertical orientation of a rendered image (OpenGL's
framebuffer origin is bottom left, the other two are top left).

## The documents

| Document | Subject |
| --- | --- |
| [01-layering-and-specialization.md](01-layering-and-specialization.md) | how one API-neutral body of code gets three backends |
| [02-api-abstraction.md](02-api-abstraction.md) | the neutral vocabulary: types, formats, states, modes, device features |
| [03-renderer-and-frame.md](03-renderer-and-frame.md) | `BaseRenderer`, matrices, views, viewports, scene and screen |
| [04-command-lists.md](04-command-lists.md) | render tasks, the list stack, temporary lists, resource lifetime |
| [05-render-targets-and-draw-buffers.md](05-render-targets-and-draw-buffers.md) | render target stacking and draw buffer selection |
| [06-buffers-and-meshes.md](06-buffers-and-meshes.md) | `GfxDataBuffer`, layouts, meshes, `GfxArray`, texture hand-over |
| [07-textures-and-samplers.md](07-textures-and-samplers.md) | texture shapes, upload, samplers, bindless textures, atlases |
| [08-shaders.md](08-shaders.md) | shader sources, compilation, parameters, caches |
| [09-components.md](09-components.md) | the ready-made drawing components |
| [10-vr-and-openxr.md](10-vr-and-openxr.md) | OpenXR, multiview, VR switching and the graphics device restart |

## Reading level

These documents describe architecture: what the pieces are, why they are cut the way they are, and
what a decision costs elsewhere. They name classes and files but avoid signature lists - the headers
carry those, and they carry unusually detailed comments where a decision is not obvious from the
code. Where a detail *is* the reason for an architectural decision, it is stated.

Each document ends with a section on what currently differs between the three backends or between the
built state and the intended state. Those sections are the ones that go stale first.
