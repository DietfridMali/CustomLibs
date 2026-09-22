# rendertools - Overview

`rendertools` is a rendering library. It sits between an application's own renderer and a graphics
API, and it exists in three interchangeable builds: OpenGL, Vulkan and DirectX 12. An application is
written once, against the library's vocabulary, and picks a backend at build time.

Two applications use it today: *Paintjob Rampage* (Smiley Battle) and *D2X-XL*. Neither of them
contains graphics API calls of its own; everything that talks to a driver lives here.

## What belongs in the library, and what does not

The library owns everything that is the same regardless of what is being rendered:

- device, swap chain, command submission, resource lifetime
- render targets, viewports, render passes, draw buffer selection
- vertex/index/storage buffers, meshes, and the layouts that describe them
- textures in all their shapes, their upload paths, samplers and mip chains
- shaders: source, compilation, caching, parameter binding
- ready-made drawing components (quad, cube, sphere, billboard, skybox, lines, lightning, text,
  shadow map, noise)

The application owns what is being rendered, in what order, and with which parameters. It derives
from the library's renderer class and adds its own passes; it does not reach past the library to the
API. The standing rule in both applications is that a missing feature is added *to* the library
rather than worked around next to it.

Below the library sits `basetools`, a separate library with the containers, string, math and file
types the code is written in (`AutoArray`, `List`, `String`, `Matrix4f`, `Vector3f`, ...). Those types
are kept free of rendering concerns; `rendertools` depends on them and not the other way round.

## Directory layout

```
rendertools/
    include/            API-neutral headers, shared by all three backends
    src/                API-neutral implementation, shared by all three backends
    opengl/
        include/        OpenGL definitions of the per-backend headers
        src/            OpenGL implementation
        visualstudio/   project file producing the OpenGL build of the library
    vulkan/             same three, for Vulkan
    directx/            same three, for DirectX 12
```

The split is the central structural decision of the library and is described in
[01-layering-and-specialization.md](01-layering-and-specialization.md). In short: a name such as
`GfxRenderer`, `Shader`, `Texture`, `RenderTarget`, `CommandList`, `GfxStates` exists once per
backend, in a file of the same name, and the build decides which one is compiled. Shared code
includes `"shader.h"` and gets the shader of whichever backend it is being built for.

## Choosing a backend

Each backend has its own project file, and all three produce a library of the same name. They differ
in two things:

- the preprocessor symbol: `OPENGL`, `VULKAN` or `DIRECTX`
- the include path: shared `include/` plus the backend's own `include/`

Both are set in `opengl|vulkan|directx/visualstudio/rendertools.vcxproj`. The Vulkan and DirectX
builds additionally define `GLM_FORCE_DEPTH_ZERO_TO_ONE`, because their clip space depth range is
`[0,1]` where OpenGL's is `[-1,1]`.

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
| [02-api-abstraction.md](02-api-abstraction.md) | the neutral vocabulary: types, formats, states, modes |
| [03-renderer-and-frame.md](03-renderer-and-frame.md) | `BaseRenderer`, matrices, viewports, scene and screen |
| [04-command-lists.md](04-command-lists.md) | render tasks, frame list, temporary lists, resource lifetime |
| [05-render-targets-and-draw-buffers.md](05-render-targets-and-draw-buffers.md) | render target stacking and draw buffer selection |
| [06-buffers-and-meshes.md](06-buffers-and-meshes.md) | `GfxDataBuffer`, layouts, meshes, texture hand-over |
| [07-textures-and-samplers.md](07-textures-and-samplers.md) | texture shapes, upload, samplers, atlases |
| [08-shaders.md](08-shaders.md) | shader sources, compilation, parameters, caches |
| [09-components.md](09-components.md) | the ready-made drawing components |

## Reading level

These documents describe architecture: what the pieces are, why they are cut the way they are, and
what a decision costs elsewhere. They name classes and files but avoid signature lists - the headers
carry those, and they carry unusually detailed comments where a decision is not obvious from the
code. Where a detail *is* the reason for an architectural decision, it is stated.

Each document ends with a section on what currently differs between the three backends or between the
built state and the intended state. Those sections are the ones that go stale first.
