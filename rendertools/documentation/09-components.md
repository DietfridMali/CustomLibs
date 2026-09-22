# Components

Above the abstraction layers sits the part of the library an application actually calls: finished
things that draw. They are what makes `rendertools` a rendering library rather than an API wrapper.

Almost all of them are shared code (`include/` + `src/`), built on `Mesh`, `RenderTarget`, `Shader`
and `GfxStates`. Where one needs something a backend does differently, it goes through the interfaces
of the preceding documents rather than around them.

## Geometry primitives

| Class | What it is |
| --- | --- |
| `Quad` | plane and rectangle *geometry* - normal, center, reference edges, point containment. No GPU side. |
| `BaseQuadMesh` | `Quad` + `Mesh`: the unit quad as drawable geometry, with `TransformationParams` (center origin, vertical flip, rotation) |
| `Cube` | the eight vertices plus triangle and quad index tables, as static data |
| `IcoSphere` | a sphere by recursive subdivision of a base solid, with vertex deduplication |
| `Rectangle` | integer rectangle, base of `Viewport` |
| `LineSegment` | a segment with intersection solutions - geometry, used for collision, not for drawing |
| `Movement` | velocity / scale / normal with an intended-actual-remaining breakdown |

`BaseQuadMesh` is the most used object in the library: the renderer's render quad, every
post-processing step, every atlas render, the billboard, the frame counter overlay. `Quad` and `Cube`
also serve as pure geometry for code that needs the shape but not a draw.

`IcoSphere`'s deduplication (`VertexKey`) is what keeps a subdivided sphere from growing a separate
vertex per face - subdivision produces the same new vertex from each of the two faces sharing an edge.

## Drawing components

**`Billboard`** is a `BaseQuadMesh` with an icon texture, oriented from three points. The library's
answer to "draw this image out there in the world".

**`LineRenderer`** replaces what `glLineWidth` did and DirectX has no counterpart for. Every line is
one instance of a unit quad that the vertex shader expands into a ribbon in view space; the fragment
shader draws a capsule distance field into it, so ends are round, strip joints close by themselves and
the edge is antialiased analytically. The width is in *pixels of the target* and stays that whatever
the distance, under a perspective and under an orthographic projection alike.

Its usage shape is `Clear()`, `Add()` / `AddStrip()` as often as wanted, `Render()` once: the records
are collected on the CPU and uploaded on render. Each batch of a frame goes behind the one before it
in the buffer - because the GPU copies run when the frame is submitted, so batches sharing a range
would all show the last one - and the shader is told the batch's start. The buffer grows on demand.

It also illustrates the render state contract: the renderer sets alpha blending and no face culling
for its draw and puts both back, while depth test and write are left to the caller, because a HUD line
wants them off and a line in the scene wants them on.

Patterns (Solid, Dashed, Dotted, DashDot) are expressed in multiples of the line width, and
`AddStrip` keeps the pattern running across joints.

**The lightning system** is four classes: `LightningBolt` (one discharge), `LightningEmitter`
(re-ignition over time), `LightningNoise` (the displacement), and `LightningSystem` (a bundle that
shares a lifecycle and moves together). A system owns its bolts by raw pointer as single owner and is
non-copyable to keep that safe in containers. The noise properties - kink sharpness, fBm octaves and
gain - describe the *kind* of discharge and therefore live once on the system, with the bolts pointing
at them. A time to live of zero or less means permanent; otherwise the handler reaps the system when
it elapses.

**`Skybox`** holds a 3x3 table of cube maps, a cloud noise texture, blue noise and the mesh. Its
setup takes a cap on the cube map face edge
length: zero asks for the largest set the GPU can hold, and an application that ships only one size
says so. A *cap*, not a demand.

**`ShadowMap`** owns a render target, the light transform, the model-view transform and the eight NDC
frustum corners it fits the light's view to. The two flags (`m_renderShadows`, `m_applyShadows`) are
separate on purpose: rendering the map and using it are different decisions.

**`PrerenderedTexture`** / `PrerenderedItem` render something once into a target and keep it - menu
backgrounds, text that does not change per frame.

## Text

Three layers:

- **`FontHandler`** loads a font (SDL_ttf) and produces glyphs, each with its texture and metrics,
  into a texture atlas. `TextDimensions` is what a caller measures with.
- **`TextEffects`** is the decoration layer: outline width and color, and an antialias method.
  `Decoration::HaveOutline` / `ApplyAA` are what the renderer branches on.
- **`TextRenderer`** derives from `TextEffects` and is the singleton an application calls.

The glyph atlas is the canonical user of the atlas-versus-array rule from
[07-textures-and-samplers.md](07-textures-and-samplers.md) - cells sit flush, so no filtering and no
mip maps - and of `MeshHandler`'s pool, because a text mesh is rebuilt every frame.

## Atlases

`BaseTextureAtlas` (shared half), `TextureAtlas` (fixed grid), `VariableTextureAtlas` (shelf packing).

`SkylinePacker` is the packing algorithm, and it is deliberately **geometry only** - no texture, no
render target, nothing that costs the GPU anything. The separation is the point: a caller that wants
to try a packing, a different ordering of the same rectangles to see whether it needs fewer pages, can
run thousands of packings without allocating a single texture, and the one it keeps is produced by
exactly the code that was tried.

## Noise

| Class | What |
| --- | --- |
| `Noise` | value / Perlin noise with seed, cells per axis, normalization and a warping mode (Infinite, Periodic, None) |
| `FBM<NoiseFn>` | fractal Brownian motion over any noise functor: frequency, lacunarity, gain, octaves, folding |
| `BaseNoiseTexture` / `NoiseTexture` | generated noise as a GPU texture, with the shared half in the base |
| `NoiseTexture3D`, `CloudNoiseTexture` | the volume variants the cloud and sky renderers sample |

`FBM` templates on the noise function rather than deriving from it, and its functor member is
`mutable` because a noise functor typically keeps working memory that is not part of the FBM's logical
state.

## Post-processing

`BilateralBlur` is the parameter object of the shared bilateral blur shader, and its header is a good
example of how a shared component handles a genuine variation: what differs between its two users is
where the *surface distance* comes from.

- `dsWorldPosition` binds a world position G-buffer and takes the neighbour's offset from the centre
  pixel's tangent plane;
- `dsSceneDepth` binds the scene depth buffer and takes the difference of two linearized eye space
  depths, with the projection matrix's A and B passed so neither near nor far plane has to be.

Each source has its own shader to deploy, and `distanceSource` names the same choice for the OpenGL
program, which branches on it. Binding the textures is the caller's business - only it knows which
target they live on.

The tone mapper, outline and blur shaders are in the shader inventory described in
[08-shaders.md](08-shaders.md).

## Loaders

- **`DDSLoader`**: block-compressed textures with pre-built mip chains. `IsDDSFile` is what routes a
  file to it instead of to the SDL image loader.
- **`GLBLoader`**: glTF/GLB via tiny_gltf, producing meshes and textures.
- **`json.hpp`** and **`tiny_gltf.h`** are the two third-party headers in the library, both used only
  by the GLB path.

## Readback

`BaseReadTarget` plus each backend's `GfxReadTarget` is the asynchronous readback: a state machine
(`Idle`, `Pending`, `Ready`) over a destination buffer, fed by `RenderTarget::ReadBufferAsync`. The
synchronous `RenderTarget::ReadBuffer` drains the pipeline; this one does not.

## Ray tracing

`AccelerationStructure` exists in all three backends as a header, with substance only in Vulkan
(`VK_KHR_acceleration_structure`). Two levels as the API demands: a bottom level structure is a BVH
over triangles, built once per geometry; a top level structure is a BVH over instances, each naming a
bottom level structure and a transform, cheap enough to rebuild every frame. A moving object changes
its instance transform and its bottom level structure is never touched again.

One decision in it is worth reading as an example of how the buffer layer constrains what sits on top:
**the geometry is copied**. A build takes device addresses of vertex and index buffers, and those must
stay valid for as long as the structure is used - which a `GfxDataBuffer` cannot promise, because a
dynamic buffer rotates between frame slots and a level mesh lays its index buffer out again whenever
its batches change (see [06-buffers-and-meshes.md](06-buffers-and-meshes.md)). So the class allocates
and owns buffers of its own.

## Instrumentation

- **`FrameCounter` / `MovingFrameCounter`**: a moving-average frame rate that can draw itself as an
  overlay.
- **`GpuTimer`**: GPU timing queries, ring-buffered over three frames in flight to avoid stalls.
- **`gpuzone.h`**: `GfxGpuZone(name)` - a GPU profiling zone spelled the same in every backend.
  OpenGL's Tracy zone takes a name alone, while DX12 and Vulkan need the Tracy context and the command
  list the timestamps are written on, which only the library knows. That is the whole reason the
  header exists.
- **`GfxStates::DrawCount`**: the draw counter, described in [03-renderer-and-frame.md](03-renderer-and-frame.md).

## Windowing

`SDLHandler` and `BaseDisplayHandler` (per backend) own SDL initialization, the window, the display
mode list and the swap chain. `BaseDisplayHandler` is described with the frame in
[03-renderer-and-frame.md](03-renderer-and-frame.md).

## Divergences

- `Cube` is a `BaseSingleton` although it holds only static data.
- `EXTERNAL_ATLAS` is defined to 1 in both `fonthandler.h` and `textrenderer.h` with no `#undef`
  guard, so the two definitions have to stay in agreement by hand.
- `USE_STATIC_GFX_DATA` is defined to 0 in `base_quadmesh.h`, next to a commented-out static
  `GfxDataLayout*` member.
- `AccelerationStructure` headers exist in all three backends; only the Vulkan one has an
  implementation (`vulkan/src/acceleration_structure.cpp`).
- `opengl/src/ogl_shadowmap.cpp` is not in the OpenGL project file; the shared `src/shadowmap.cpp` is
  what all three backends build.
