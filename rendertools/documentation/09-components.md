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
post-processing step, every atlas render, the billboard, the line renderer's ribbon and every glyph
the text renderer draws (the frame counter overlay is text). `Quad` and `Cube` also serve as pure
geometry for code that needs the shape but not a draw.

`IcoSphere`'s deduplication (`VertexKey`) is what keeps a subdivided sphere from growing a separate
vertex per face - subdivision produces the same new vertex from each of the two faces sharing an edge.

## Drawing components

**`Billboard`** is a `BaseQuadMesh` with an icon texture, oriented from three points. The library's
answer to "draw this image out there in the world".

**`LineRenderer`** replaces what `glLineWidth` did and DirectX has no counterpart for. Every line is
one instance of a small mesh of three quads that the vertex shader expands into a ribbon in view
space: the middle piece runs exactly from one end to the other, and the two end caps are laid out
along the line's direction *on screen*, not along its 3D axis - spanned along the axis, the caps of a
line seen nearly end on reached far towards the eye and showed as a wedge. The fragment shader draws a
capsule distance field into the ribbon and antialiases the edge analytically.

The width is in *pixels of the target*, under a perspective and under an orthographic projection
alike. Two optional modifiers change that: a *world width* caps the pixel width at what that width in
world units projects to, so distant lines get thinner, though never thinner than 0.75 pixels; and a
*viewer pull* moves both ends towards the eye by an amount that falls off with their distance - the
same pull d2x-xl applies to its level outlines.

Each end of a line has a cap of its own: round (the capsule), flat (with an antialiased edge and an
overhang in pixels) or mitered (a straight cut, given as a slope). The shader never looks at
neighbouring lines; a joint is composed by the caller from two lines whose cuts match, and
`MiterSlope` computes such a cut on the CPU, in the same view space frame the shader uses. The caps
ride in the padding of the line record, so the record kept its layout.

Its usage shape is `Clear()`, `Add()` / `AddStrip()` as often as wanted, `Render()` once: the records
are collected on the CPU and uploaded on render. Each batch of a frame goes behind the one before it
in the buffer - because the GPU copies run when the frame is submitted, so batches sharing a range
would all show the last one - and the shader is told the batch's start. The buffer grows on demand.
A line set that does not change can be made *resident* instead: `Upload()` copies it once, and every
`Render()` after that draws it without a copy until the next `Clear()`.

The vertex shader reads the records from a read-only structured buffer (`t0 space1`, bound with
`BindReadOnly`), not from a writable one: Vulkan allows writable storage buffers in the vertex stage
only with the optional `vertexPipelineStoresAndAtomics` feature (`GfxFeature::StorageInVertexStage`).
The lightning shaders read their records the same way, so a caller that binds the lightning buffers
itself has to use `BindReadOnly` as well.

It also illustrates the render state contract: the renderer sets alpha blending and no face culling
for its draw and puts both back, while depth test and write are left to the caller, because a HUD line
wants them off and a line in the scene wants them on.

Patterns (Solid, Dashed, Dotted, DashDot) are expressed in multiples of the line width; `AddStrip`
keeps the pattern running across joints, and a single `Add` takes the phase explicitly.

**The lightning system** generates the geometry of electric discharges; the application draws it,
with the `lightningDraw` and `lightningFlare` shaders the library ships. Its classes:

- `LightningBolt` - one jagged polyline: `Build` displaces it off a straight line, `BuildOnPath` off a
  guide path over a surface.
- `BaseLightning` - a bundle of bolts of one appearance between two end points, in two kinds:
  `LightningStrike` (one-shot, a trunk with recursive branches, faded over its lifetime) and
  `LightningArc` (persistent, parallel bolts without branches that writhe in their envelope).
- `LightningNoise` - the fBm noise both displacement layers are made of.
- `LightningEmitter` - re-ignition over time and the endpoint policy of an ignition.
- `LightningSystem` - a bundle that shares a lifecycle and moves together.
- `LightningLook` - the application-wide look (segment length, branch statistics, width ratio,
  flicker), a singleton (`lightningLook`).

A system owns its lightnings by raw pointer as single owner and is non-copyable to keep that safe in
containers; each lightning holds its bolts. The noise properties - kink sharpness, fBm octaves and
gain - describe the *kind* of discharge and therefore live once on the system, taken from the creation
parameters of the lightnings added, with every lightning pointing at them. A time to live of zero or
less means permanent; otherwise the handler reaps the system when it elapses.

The node count of a bolt follows from its length: a multiple of `referenceSamples` for bolts longer
than the calibration length, and for shorter ones `referenceSamples` divided by its smallest integer
divisor that keeps the step above the minimum segment length - a short branch gets fewer features,
not finer ones, and every bolt is a scaled copy of the same pattern. `sampleScale` divides the length
first, so a lightning drawn scaled down keeps the node count of the unscaled one.

A strike can flash several times (`strokeCount`): each return stroke is full at once and decays, the
gaps between them are drawn from the strike's seed, and every stroke builds a new shape and re-fires
the ignition flash of the core. Without strokes a strike flickers and fades over its lifetime. A
strike may state its own branch length factor instead of the look's.

In random direction mode an emitter draws the end of every lightning from a hollow cone between two
angles around the reference direction - both at 90 degrees put all ends on a circle - and can spread
the lightnings of one ignition evenly (a sector each around the axis, a Hammersley sequence over the
cone) instead of drawing them independently. The start offset that keeps bolts off an object's centre
is drawn from a range whose width `startOffsetJitter` sets, and can move the start alone.

A strike can also run *along a surface*. The application implements `LightningSurface`, which traces
a path over its geometry from a point in a direction for a length. `LightningStrike::SetupOnSurface`
(reached through `LightningSystem::AddStrike` with a path) decides the branch structure once, traces
every branch over the surface and keeps the paths; every later rebuild only renews the displacement.
`BuildOnPath` swings the bolt within the surface, lets it leave the surface only outward, and puts
nodes onto the folds of the path.

`LightningSegment`, the record the renderer hands to the GPU, lives next to the geometry so every
application uses one layout. It carries the per-bolt core width and halo tint, and a slot for a
per-bolt core colour.

**`Skybox`** holds a 3x3 table of cube maps (three variants of the day sky, a star map and a night
sky), a cloud noise texture, blue noise and the mesh. Its setup takes a cap on the cube map face edge
length: zero asks for the largest set the GPU can hold, and an application that ships only one size
says so. A *cap*, not a demand - which is why `FaceSize` reports the edge length that was actually
loaded, so a caller can choose resolution dependent parameters such as a filter radius.

Each sky type can be given a painted look with the Kuwahara filter (`ApplyKuwaharaFilter`), with its
own parameters per sky, and skies the application never shows can be left out. The filtered faces are
looked for first as files of their own (face name plus a suffix, a PNG or its DDS sibling); when they
are missing, the source PNGs are filtered and written as those files, and only when the sources are
missing too is the loaded sky filtered in place. `HasNightSky` tells whether a sky type has textures
of its own; the black hole renders on the star map.

**`ShadowMap`** owns two depth render targets of the same size, the light transform and the
model-view transform. The first holds the static shadow casters, the second (`StartRender(true)`,
`DynamicShadowTexture`) the moving ones. Both share the light transformation, so a shader looks up
both with the same shadow coordinate and takes the darker result, and an application can render the
static casters once and keep them over several frames. The light transformation only changes in
`Update`; in the frames between, `UpdateTransformation` adapts the view dependent matrix to the
current camera. `HasStaticCasters` tells whether the first target holds a rendered state,
`Invalidate` and `MakeReadable` reset that, and `IsDynamicPass` tells which target is being rendered.
The maps start at 8192 squared, or what the GPU allows, and halve until both targets can be created,
down to 1024.

The two flags (`m_renderShadows`, `m_applyShadows`) are separate on purpose: rendering the map and
using it are different decisions.

**`ShadowAtlas`**, next to it in `shadowmap.h`, is a depth-only render target cut into square tiles,
for one shadow map per object. `Layout` picks the largest tile size between the configured maximum and
minimum that still fits the requested number of tiles, `TileViewport` places a tile, and
`PointLightTransformation` builds the perspective frustum from a point light onto an object's bounding
sphere: the half field of view from the tangent cone, the near plane at the sphere, the far plane
where the caller wants it (at least behind the sphere).

**`PrerenderedItem`** and its two derivatives `PrerenderedText` and `PrerenderedImage` (all in
`prerenderedtexture.h`, one shared implementation for the three backends) render something once into
a target and keep it - text that does not change per frame, an outlined image. The item chains its
passes through the target's buffers: the content, then optionally a bevel and an outline, each
reading the last destination and writing the next. `PrerenderedText` takes one colour or one colour
per glyph, cycling through the list when it is shorter than the text.

## Text

Three layers:

- **`FontHandler`** opens a font through SDL3_ttf and works in two modes. `Create` renders a glyph
  set, each glyph with its texture and metrics, into a texture atlas; `TextDimensions` is what a
  caller measures with. `Open` only opens the font, for a caller that builds an atlas of its own and
  does its own layout: it asks for single glyphs (`HasGlyph`, `GetGlyphMetrics`, `RenderGlyph`) and
  for the font's vertical metrics (`Ascent`, `Descent`, `LineHeight`). Scripts that need shaping -
  right to left, Arabic, Thai - cannot be assembled glyph by glyph: `SetShaping` sets direction and
  script, and `CreateTextTexture` renders a whole UTF-8 string into one texture, without an atlas.
  Text is walked as UTF-8 sequences (`NextGlyph`), so a glyph is a code point, not a byte.
- **`TextEffects`** is the decoration layer: outline width and color, and an antialias method.
  `Decoration::HaveOutline` / `ApplyAA` are what the renderer branches on.
- **`TextRenderer`** derives from `TextEffects` and is the singleton an application calls. Besides
  one colour it takes a colour per glyph (`SetGlyphColors`); the glyph quads then carry vertex colours
  and are drawn with the `coloredTexture` shader (texture times vertex colour times surface colour).

A text mesh is rebuilt every frame and comes from `MeshHandler`'s pool. The glyph atlas does not
follow the strict atlas rule of [07-textures-and-samplers.md](07-textures-and-samplers.md): it is
created with a padding of one texel around every cell and linear filtering.

## Atlases

`BaseTextureAtlas` (shared half), `TextureAtlas` (fixed grid), `VariableTextureAtlas` (skyline
packing, for tiles of differing size).

`TextureAtlas` takes the filter mode, a cell padding and a number of mip levels; the default stays one
level, nearest filtering and no padding. Mip levels break the atlas rule of
[07-textures-and-samplers.md](07-textures-and-samplers.md) and pay for it with padding: the padding
grows to at least 2 ^ (levels - 1) texels of the scaled target, so a filter reading the smallest level
stays inside the cell's own margin. The owner calls `BuildMipMaps` after the last `Add`: it reads
level 0 back, builds the chain on the CPU with an alpha weighted box filter (`BuildAlphaMipChain2D`,
`texture_mips.h`) so the transparent texels around a glyph do not darken its edge, and writes every
further level. OpenGL render targets have a single level, so there the atlas stays without mips.

`SkylinePacker` is the packing algorithm, and it is deliberately **geometry only** - no texture, no
render target, nothing that costs the GPU anything. The separation is the point: a caller that wants
to try a packing, a different ordering of the same rectangles to see whether it needs fewer pages, can
run thousands of packings without allocating a single texture, and the one it keeps is produced by
exactly the code that was tried.

## Noise

| Class | What |
| --- | --- |
| `NoiseParams` | seed, cells per axis, normalization, a warping mode (Infinite, Periodic, None), and the fBm parameters of the cloud noise channels |
| `Noise` (namespace) | the CPU generators - Perlin and improved Perlin noise, simplex variants, Worley noise, hashes, the cloud noise combination - and the functors that hand each of them to `FBM` |
| `FBM<NoiseFn>` | fractal Brownian motion over any noise functor: frequency, lacunarity, gain, octaves, folding |
| `BaseNoiseTexture<Tag>` / `NoiseTexture<Tag>` | generated 2D noise as a GPU texture, with the shared half in the base; the tag's `NoiseTraits` give format, sampling and generator (value, Perlin, fBm, foam, hash, weather) |
| `NoiseTexture3D`, `CloudNoiseTexture`, `DetailNoiseTexture` | the volume variants the cloud and sky renderers sample |
| `BlueNoiseTexture` | a 3D stack of blue noise slices, loaded from a file the application ships |

`FBM` templates on the noise function rather than deriving from it, and its functor member is
`mutable` because a noise functor typically keeps working memory that is not part of the FBM's logical
state.

`FoamNoiseTexture` is a tileable 2D Worley pattern summed over several octaves (each doubling the cell
count and halving the weight) and normalized to 0 .. 1 - bright ridges between dark cells, the look of
sea foam - sampled with linear filtering and repeat. The cloud noise volume can be given a painted
look: `CreateKuwahara` filters it slice by slice through the Kuwahara filter and caches the result in
its file. Blue noise is an external resource: when its file cannot be loaded the texture reports it as
missing game data and is not created. The shaders have noise functions of their own in the HLSL and
GLSL function libraries, among them a 3D simplex noise.

## Post-processing

`BilateralBlurParams` (applied with `SetupBilateralBlur`) is the parameter object of the shared
bilateral blur shader, and its header is a good example of how a shared component handles a genuine
variation: what differs between its two users is where the *surface distance* comes from.

- `dsWorldPosition` binds a world position G-buffer and takes the neighbour's offset from the centre
  pixel's tangent plane;
- `dsSceneDepth` binds the scene depth buffer and takes the difference of two linearized eye space
  depths, with the projection matrix's A and B passed so neither near nor far plane has to be.

Each source has its own shader to deploy, and `distanceSource` names the same choice for the OpenGL
program, which branches on it. Binding the textures is the caller's business - only it knows which
target they live on.

**`KuwaharaFilter`** is an anisotropic Kuwahara filter on the GPU - a painted look that flattens
regions and keeps edges. It runs in two passes: a smoothed structure tensor of the source, then the
filter itself, whose sector kernel is stretched along the local orientation. It is used in three ways:

- `Apply` / `ApplyCube` replace a texture by its filtered image. The result is read back and the
  texture deployed again from it, so the filter costs nothing afterwards. A cube map is filtered across
  its edges - each face is extended by a margin taken from its neighbours - and a cube map created with
  mip maps gets its chain rebuilt from the filtered faces. An optional Gauss blur (`blurRadius`)
  smooths the result.
- `FilterToTarget` filters into the filter's own render target and leaves the result there, without a
  read-back - for a filter that runs every frame, on an optional area only and, with two views, on the
  VR eye images. `Prepare` creates the targets for a size ahead of time so the first use does not
  allocate; the targets are cached per size, format and view count.
- `FilterToBuffer` filters a single channel float image and reads it back into a float buffer, which
  is how the cloud noise volume is filtered slice by slice.

The filter's shaders exist for Vulkan and DirectX only. The tone mapper, outline and blur shaders are
in the shader inventory described in [08-shaders.md](08-shaders.md).

## Ocean

**`OceanFFT`** computes an ocean surface on the GPU with the FFT method: a wave spectrum is evolved in
time in frequency space and transformed back into a displacement field every frame. Three cascades of
different tile size (by default about 1000, 100 and 10 world units) each own a band of wave numbers,
so swell, wind sea and ripples come from one spectrum and no wave is counted twice. The spectrum is
built once on the CPU from two wave systems, swell and wind sea, each with a length, heading, spread,
height and peakedness; the random amplitudes are seeded, so the same parameters give the same sea.

Per frame, compute shaders evolve the spectrum, run a radix-2 FFT over all three cascades at once
(they sit side by side in one work target), and assemble per cascade a displacement map (horizontal
displacement scaled by the choppiness, and height) and a derivative map (the slopes and the
derivatives of the horizontal displacement), both 256 x 256 in half floats. The fourth channel of the
displacement map holds the *compression* of the surface - one minus the Jacobian of the horizontal
displacement - carried over from the previous frame and decaying with `foamDecay`. It is the raw
quantity rather than a finished foam value, so how much foam it means is left to the shading; and
because it carries over, each cascade has two displacement targets that alternate.

The targets are compute buffers of render targets, read as sampled and written as storage images.
OceanFFT exists for Vulkan and DirectX; it is not part of the OpenGL project.

## Camera paths

**`PoseSpline`** turns a list of keys - a position, an orientation and the speed of the segment that
starts at the key - into a camera path evaluated by time.

- *Position* is a centripetal Catmull-Rom spline in Hermite form. On an open path the first and the
  last segment are arcs: the tangent at the inner key is mirrored at the chord, where a phantom key
  mirrored through the end key would aim the end tangent straight at the neighbour and make the curve
  bulge out. A segment between coincident keys keeps its position, and two keys give a straight line.
- *Timing* is by speed, not by duration - a fixed duration per segment made the camera crawl at the
  keys and race between them whenever segments differed in length. `StartTiming` simulates the run in
  2 ms steps into a distance table: from rest, with an S-curve ramp to each segment's speed from the
  moment its key is reached, and braking along the same curve so the camera stops exactly at the last
  key. An arc length table per segment maps distance to the curve parameter, so the speed is the speed
  along the curve. The run can start at any key.
- *Orientation* runs on the same share of the segment as the position. Per segment it takes one of
  three forms, decided by where the horizontal view directions of its two keys meet: a *turn*, a
  cumulative cubic Bezier quaternion curve whose turn rate at a key is the length-weighted mean of the
  two adjacent segments, so the turn runs on through the key without a jolt; a *focus*, where both keys
  look at (or away from) a common point and the camera keeps that point in view while it moves; or a
  *swing*, where the start key looks away from the point the end key looks at and the view turns half
  around about the vertical within a given swing time centred on the key.
- A path of at least four keys whose last key lies close to its first is a closed loop: it wraps
  around without braking and keeps running.

`Point` samples the curve for drawing it. PoseSpline is plain math on glm quaternions; it is part of
the Vulkan and DirectX projects, not of the OpenGL one.

## VR view

**`XRView`** is the part of the VR support an application calls: it polls for a headset, switches VR
on and off in steps a progress display can follow (the application implements `XRSwitchClient` to
release and recreate its view dependent buffers), and provides the eye projections with a widened
field of view and a view pitch, the visibility mask and the background in the headset. It sits on the
API-neutral OpenXR core and the backend's handler. The VR architecture is described in
[10-vr-and-openxr.md](10-vr-and-openxr.md).

## Loaders

- **`ddsloader.h`**: block-compressed textures with pre-built mip chains - BC1, BC4, BC5 and BC7, from
  a file or from memory. `LoadTextureFile` is the one place every backend's texture loading goes
  through and the one place the format is decided: on a device with block compression a `.DDS` (or
  `.dds`) next to the requested file is preferred over it, a DDS on a device without block compression
  is refused, and anything else goes to the SDL image loader. `LoadTextureMemory` decides the same for
  an image held in memory, by its magic. Callers that need pixels rather than a texture get CPU
  decoders for BC7 and BC4 (`DecodeCompressedRows`, by ranges of block rows so the work can be spread
  over threads) and for the BC7 alpha alone (`LoadCompressedAlpha`); `ReadImageExtent` reads the size
  of a PNG, DDS or TGA from its header alone.
- **`GLBLoader`** has two entry points. `Load` is the path built for one particular model - the smiley
  of Paintjob Rampage - which it corrects and delivers as a triangle soup with shape keys, cached in a
  binary file. `LoadModel` reads general glTF and GLB models: indexed meshes, one part per primitive
  with its material (base colour image, wrap modes, alpha cutoff, double sided), texture coordinates,
  vertex colours, and sparse accessors. Images stay encoded, only those used as base colour are read,
  and an image with a DDS next to it is not read at all - the caller loads the compressed one by the
  name the loader keeps for every image.
- **`ObjLoader`** reads Wavefront `.obj` files with their `.mtl` material libraries into the same mesh
  data as `GLBLoader::LoadModel`: indexed, one part per material run, the diffuse colour baked into the
  vertex colours, the diffuse map as base colour image, alpha testing where the material dissolves. A
  separate alpha map is not supported; the alpha of the diffuse map is used.
- **`json.hpp`** and **`tiny_gltf.h`** are the two third-party headers in the library, both used only
  by the glTF path. tiny_gltf is compiled without its image decoder: images are kept encoded and
  decoded by the library's own loaders.

Every loader reports a required file it cannot load to basetools' `MissingFiles`.

## Readback

`BaseReadTarget` plus each backend's `GfxReadTarget` is the asynchronous readback: a state machine
(`Idle`, `Pending`, `Ready`) over a destination buffer, fed by `RenderTarget::ReadBufferAsync` and - in
Vulkan and DirectX - by `GfxArray::DownloadAsync`, which records the copy of a whole GPU buffer into
the command list that is recording. The caller polls the read target. The synchronous
`RenderTarget::ReadBuffer` drains the pipeline; this one does not.

## Ray tracing

`AccelerationStructure` is implemented in Vulkan (`VK_KHR_acceleration_structure`) and in DirectX
(DXR 1.1, inline ray queries) with the same signatures, and in both a shader reaches the top level
structure as a `RaytracingAccelerationStructure` on `t0 space2`. OpenGL carries a header-only stub
whose builds fail, so shared application code compiles there and takes its path without ray tracing.

Two levels as the APIs demand: a bottom level structure is a BVH over triangles, built once per
geometry; a top level structure is a BVH over instances, each naming a bottom level structure and a
transform, cheap enough to rebuild every frame. A moving object changes its instance transform and its
bottom level structure is never touched again.

Bottom level structures are built in batches: many structures share one scratch allocation, one
submission and one wait for the GPU. The top level structure is built into the command list that is
recording, ordered before the draws that trace it, or in a list of its own when asked to be immediate.
It alternates between two frame slots chosen by the frame index, so a rebuild never overwrites the
structure the frame in flight is still traversing.

One decision in it is worth reading as an example of how the buffer layer constrains what sits on top:
**the geometry is copied**. A build takes vertex positions and indices as CPU arrays, not as mesh
buffers, and copies them into buffers of its own, because the build reads its input through device
addresses and a `GfxDataBuffer` offers no stable range to point at - a dynamic buffer rotates between
frame slots and a level mesh lays its index buffer out again whenever its batches change (see
[06-buffers-and-meshes.md](06-buffers-and-meshes.md)). The copies live only as long as the build: a
structure built from scratch never reads its input again, so Vulkan frees them once it has waited for
the build and DirectX hands them to the deferred release.

In a graphics restart an acceleration structure releases itself and comes back empty; the application
builds it again.

## Instrumentation

- **`BaseFrameCounter`**, with `MovingFrameCounter` (the average over the last 90 frames; the one
  `BaseRenderer` owns) and `LinearFrameCounter` (in total and over the last second): a frame rate that
  can draw itself as a text overlay through the text renderer.
- **`GpuTimer`**: OpenGL timer queries, ring-buffered over three frames in flight to avoid stalls. The
  Vulkan and DirectX headers carry the same code compiled out; GPU timing there is the frame metrics
  below.
- **`gpuzone.h`**: `GfxGpuZone(name)` - a GPU profiling zone spelled the same in every backend.
  OpenGL's Tracy zone takes a name alone, while DX12 and Vulkan need the Tracy context and the command
  list the timestamps are written on, which only the library knows. For the same reason DX12 and
  Vulkan use a zone scope of the library's own instead of Tracy's: the end timestamp goes into
  whatever command list is recording when the zone is left, because a zone can outlive the list it
  began in when a render target switch closes that list.
- **`log_frame_metrics.h`**: `LOG_FRAME_METRICS`, 0 unless the project sets it, compiles in the GPU
  frame timing of Vulkan and DirectX - timestamps per command list and per stage mark, a summary line
  per second and a line for every frame whose GPU time exceeds the display period - together with the
  OpenXR frame and performance logs. The GPU timing runs only while an OpenXR session is running.
- **`GfxStageMark(name)`**: a named mark in the recording command list that splits a pass into stages
  for the frame metrics (and for the Vulkan stall diagnosis). A free function the Vulkan and DirectX
  backends define and the application declares itself.
- **`GfxStates::DrawCount`**: the draw counter, described in
  [03-renderer-and-frame.md](03-renderer-and-frame.md).

## Windowing

`SDLHandler` and `BaseDisplayHandler` (per backend) own SDL initialization, the window, the display
mode list and the swap chain; Vulkan and DirectX run on SDL3. `BaseDisplayHandler` is described with
the frame in [03-renderer-and-frame.md](03-renderer-and-frame.md).

## Divergences

- `Cube` is a `BaseSingleton` although it holds only static data.
- `EXTERNAL_ATLAS` is defined to 1 in both `fonthandler.h` and `textrenderer.h` with no `#undef`
  guard, so the two definitions have to stay in agreement by hand; nothing in the library tests it.
- `USE_STATIC_GFX_DATA` is defined to 0 in `base_quadmesh.h`, next to a commented-out static
  `GfxDataLayout*` member; the static it switches is declared as `staticGfxData` but defined as
  `staticGfxDataLayout`, so switching it on would not link as is.
- `AccelerationStructure` is implemented in Vulkan and DirectX; OpenGL has a header-only stub. The
  Vulkan header comment still says the geometry copies live as long as the structure and that DirectX
  fails; the OpenGL one that the real implementation lives in the Vulkan tree only.
- `opengl/src/ogl_shadowmap.cpp` is not in the OpenGL project file; the shared `src/shadowmap.cpp` is
  what all three backends build.
- `ShadowMap` declares eight NDC frustum corners (`m_ndcCorners`) that nothing reads.
- `KuwaharaFilter::FilterToTarget` does not put the caller's render states back: it leaves depth test
  and write and face culling on, blending off and the alpha blend function set. `Apply` / `ApplyCube`
  restore what they change.
- `LightningSegment` has a per-bolt core colour slot that the library's `lightningDraw` shader does not
  read; it takes the core colour from its uniform.
- OpenGL: `OceanFFT` and `PoseSpline` are not in the project; `KuwaharaFilter` is built but its shaders
  do not exist there; `FoamNoiseTexture`, `GfxArray::DownloadAsync` and `GfxStageMark` are missing;
  render targets have a single mip level, so `TextureAtlas` mip levels do not apply.
- `src/noisetexture3d.cpp` is in none of the project files or Makefiles.
- Header comments behind the code: `ddsloader.h` names BC1 and BC7 only, `lightningsystem.h` says the
  fBm parameters come from the first lightning added (each one added overwrites them), `shadowmap.h`
  says the map may shrink in `CreateMap` (that shrink is commented out), and `linerenderer.h` points to
  a per-backend line shader source that lives in `src/common-hlsl`.
