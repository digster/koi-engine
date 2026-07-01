# Architecture

This document captures the **big-picture design** of Koi Engine — the parts you
can't see by reading a single file — and the reasoning behind the structural
choices. It evolves as the engine grows; right now it describes the foundation
through Step 12 (window & render loop, the first triangle, geometry in GPU
vertex/index buffers, a 3D cube with hand-rolled math + MVP uniform + depth
buffer, a fly camera driven by keyboard/mouse input with delta-time, a **mesh**
abstraction drawn through a **scene graph** of parent/child transforms,
**textures** sampled onto those surfaces, lighting that shades them, per-object
**materials**, **model loading** from OBJ/glTF files, **shadow mapping**, a
**post-processing** chain — an off-screen HDR target run through bloom,
tone-mapping, and FXAA before it reaches the screen — **multiple lights** of
different kinds (directional/point/spot) accumulated in the shader, and
physically-based **PBR** shading via a Cook-Torrance metallic-roughness BRDF).

## Guiding principles

1. **Learn by building.** Every subsystem is introduced when a milestone needs
   it, never speculatively. Code is heavily commented; the
   [`docs/`](docs/index.html) tutorials explain the underlying graphics
   *concepts* for newcomers.
2. **Modern, explicit graphics.** We use the SDL3 **GPU API** (Metal/Vulkan/D3D12
   behind one interface) rather than the deprecated OpenGL, so the concepts we
   learn are the ones in use today.
3. **RAII everywhere.** Resources (window, GPU device) are owned by objects whose
   destructors release them, in reverse order of construction. No manual cleanup
   bookkeeping.

## Subsystem map

```
                 ┌──────────────┐
   main.cpp ───► │   Engine     │  owns lifecycle + main loop
                 └──────┬───────┘
            creates ┌───┴────┐ creates
                    ▼        ▼
            ┌────────────┐  ┌──────────────┐
            │  Window    │  │ GpuRenderer  │
            │ (SDL_Window│  │ (SDL_GPUDevice
            │  RAII)     │  │  + swapchain)│
            └────────────┘  └──────────────┘
                    ▲              │
                    └── claims ────┘  (renderer attaches its
                                       swapchain to the window)

   Log.hpp  ── cross-cutting: leveled logging used by everyone
```

| Component | File | Responsibility |
|-----------|------|----------------|
| `Engine` | [src/core/Engine.*](src/core/) | Owns subsystems + the camera + the scene graph, runs the game loop, computes delta-time, routes input, builds the demo scene (`buildScene`: meshes + textures + materials, assigned to nodes), animates + propagates transforms each frame, controls startup/shutdown order. |
| `Window` | [src/core/Window.*](src/core/) | RAII wrapper around one `SDL_Window`. Owns the OS window; the renderer attaches a swapchain to it. |
| `GpuRenderer` | [src/renderer/GpuRenderer.*](src/renderer/) | Owns the `SDL_GPUDevice`, the main pipeline, the depth texture, a shared **sampler**, the (Step 9) **shadow map** + sampler + depth-only **shadow pipeline**, and the (Step 10) **post-processing** resources (off-screen HDR scene target, half-res bloom ping-pong pair, LDR target, a clamp sampler, and four fullscreen pipelines). Is a **factory** for meshes (`createMesh`, 16- or 32-bit indices) and textures (`loadTexture`) and a **consumer** of a scene. `renderSceneAndPost` (shared by the live and capture paths) runs: **shadow pass** → **scene pass** into the HDR target (`recordScene`) → **post chain** (`runPostChain`: bloom → tone-map composite → FXAA into the final image). Owns no geometry/texture data. |
| `PostProcess` | [src/renderer/PostProcess.hpp](src/renderer/PostProcess.hpp) | Header-only, SDL-free: the `PostSettings` knobs (exposure, bloom threshold/intensity, per-effect toggles) the Engine drives from input and hands to the renderer, plus pure helpers (`halfExtent`, `luminance`, `acesToneMap`) that mirror the shader math for headless unit tests (`tests/test_post.cpp`). |
| `Material` | [src/scene/Material.hpp](src/scene/Material.hpp) | A header-only appearance bundle: a `shared_ptr<Texture>` (albedo) + **PBR** params `metallic` and `roughness` (Step 12, replacing Blinn-Phong's shininess/specStrength). Forward-declares `Texture` (stores it by `shared_ptr`). Shared by `shared_ptr`; a `Node` references one. The renderer binds it per draw. |
| `Pbr` | [src/renderer/Pbr.hpp](src/renderer/Pbr.hpp) | Header-only, SDL-free (Step 12): pure CPU mirrors of the Cook-Torrance BRDF sub-terms — `distributionGGX` (D), `geometrySmith`/`geometrySchlickGGX` (G), `fresnelSchlick` (F), plus `kPi`. The shader in `triangle.frag` is the runtime truth; these exist so the microfacet math's shape is unit-tested headlessly (`tests/test_pbr.cpp`), the same pattern as `PostProcess.hpp`/`Light.hpp`. |
| `Light` | [src/scene/Light.hpp](src/scene/Light.hpp) | Header-only, SDL-free (Step 11): a `Light` struct (type = directional/point/spot, position, direction, colour, intensity, range, spot-cone cosines, an `enabled` flag) plus pure helpers (`attenuation`, `spotFactor`, `activeLightCount`) that mirror the shader math for headless tests (`tests/test_light.cpp`). The `Engine` owns a `std::vector<Light>` and hands it to the renderer each frame, exactly like the camera + `PostSettings`. `MAX_LIGHTS` matches the shader's fixed array size. |
| `ModelLoader` | [src/renderer/ModelLoader.*](src/renderer/) | `loadModel(path)` reads a model file into a `Mesh` (positions/normals/UVs/indices, vertex color = white): `.obj` via **tinyobjloader** (de-duplicating its separate v/vt/vn indices), `.glb/.gltf` via **cgltf**. The single TU that pulls in those third-party headers — compiled warning-free (`-w`). |
| `Mesh` | [src/renderer/Mesh.*](src/renderer/) | RAII owner of one shape's vertex + index buffers (+ index count + element size: 16-bit for primitives, 32-bit for big loaded models). Holds a **non-owning** device pointer used only to release those buffers — so every `Mesh` must die before the renderer's device. Created via `GpuRenderer::createMesh`; held by `shared_ptr` so many nodes share one. Non-copyable/non-movable. |
| `Texture` | [src/renderer/Texture.*](src/renderer/) | RAII owner of one `SDL_GPUTexture` (sampled image). Same non-owning-device lifetime rule as `Mesh`. Created via `GpuRenderer::loadTexture` (load BMP → convert → upload); held by `shared_ptr`. A *sampler* (how to read it) is separate, reusable device state owned by the renderer. |
| `Primitives` | [src/renderer/Primitives.*](src/renderer/) | Free functions (`makeCubeMesh`, `makePlaneMesh`) that define a shape's vertex/index data and upload it via `createMesh`, keeping the renderer geometry-agnostic. |
| `Transform` | [src/scene/Transform.hpp](src/scene/Transform.hpp) | Header-only, SDL-free: position / Euler rotation / scale, plus `localMatrix()` returning `T·R·S` (scale → rotate → translate). Unit-tested in `tests/test_transform.cpp`. |
| `Node` | [src/scene/Node.*](src/scene/) | One element of the scene graph: a local `Transform`, an optional shared `Mesh` (shape) + `Material` (appearance), owned children (`unique_ptr`), and a cached world matrix. Draws only with both a mesh and a material. `updateWorldTransforms` walks the tree computing `world = parentWorld · local`. Unit-tested in `tests/test_node.cpp`. |
| `Camera` | [src/scene/Camera.*](src/scene/) | A yaw/pitch fly camera (position + angles). Produces a `view` matrix via `lookAt`; `processKeyboard`/`addMouseLook` update it from input. Lives above the renderer and knows nothing about the GPU; its header is SDL-free. Logic unit-tested in `tests/test_camera.cpp`. |
| `Vertex` | [src/renderer/Vertex.hpp](src/renderer/Vertex.hpp) | The CPU-side layout of one vertex (3D position + color + uv + normal). Its byte layout is the contract the pipeline's vertex attributes describe; pinned by `static_assert`s and `tests/test_vertex.cpp`. |
| `math` (`Vec`, `Mat4`) | [src/math/](src/math/) | Hand-rolled, header-only vector (`Vec2/3/4`) and column-major 4×4 matrix math (translate/rotate/scale/`perspective`/**`orthographic`**/`lookAt`). No GLM — written ourselves so nothing stays a black box. Pure, so fully unit-tested in `tests/test_math.cpp`. |
| `Shader` | [src/renderer/Shader.*](src/renderer/) | Loads a compiled shader for the active backend: `selectShaderVariant` (pure, unit-tested) picks the format/extension/entry point; `loadShader` reads the file and creates the `SDL_GPUShader` (with the right uniform-buffer and sampler counts). |
| `Log` | [src/core/Log.hpp](src/core/Log.hpp) | Leveled logging macros over SDL's logger; one place to control verbosity. |

## Data flow: one frame

The render loop (in `Engine::run`) repeats:

```
processEvents()  → SDL_PollEvent: Esc/close → stop; mouse motion → camera.addMouseLook
       │
dt + input       → dt = ΔSDL_GetTicksNS (clamped); camera.processKeyboard(polled keys, dt)
       │
animate + update → spin a couple of nodes (rotationEuler += rate·dt);
                   sceneRoot.updateWorldTransforms() caches every node's world matrix
       │
renderFrame(view,      → in GpuRenderer (view = camera.viewMatrix()); renderSceneAndPost:
  sceneRoot, cameraPos,   1. acquire command buffer + swapchain image (+ its size)
  lights, post)          2. ensureSceneTargets: depth + HDR scene + half-res bloom + LDR match size
                     3. SHADOW PASS: compute lightViewProj from the SUN's direction
                        (lights[0]); render scene depth into the shadow map (depth-only)
                     4. SCENE PASS: begin render pass into the HDR target (color + depth CLEAR);
                        recordScene — bind pipeline + shadow map, pack the light LIST +
                        lightViewProj into the light UBO once (Step 11: an array of
                        directional/point/spot lights, only the sun shadowed), then walk
                        the tree (per node: bind material texture + push material params,
                        bind mesh buffers + push {mvp,model}, draw); end pass
                     5. POST CHAIN (runPostChain): bloom bright-pass + separable blur → composite
                        (exposure, ACES tone-map, vignette, gamma) → FXAA into the swapchain image
                     6. submit command buffer (queues present)
```

See [docs/01-window-and-render-loop.html](docs/01-window-and-render-loop.html),
[docs/02-first-triangle.html](docs/02-first-triangle.html),
[docs/03-vertex-and-index-buffers.html](docs/03-vertex-and-index-buffers.html),
[docs/04-3d-cube-mvp-and-depth.html](docs/04-3d-cube-mvp-and-depth.html),
[docs/05-camera-and-input.html](docs/05-camera-and-input.html), and
[docs/06-meshes-and-scene-graph.html](docs/06-meshes-and-scene-graph.html) for the
concept-by-concept explanation.

### Shaders & the build-time toolchain

GPUs run compiled bytecode, and each backend wants a different dialect (SPIR-V for
Vulkan, MSL for Metal). We author shaders once in **GLSL** (`shaders/*.vert`,
`*.frag`) and the build compiles them in two stages:

```
shaders/triangle.vert ──glslc──► build/shaders/triangle.vert.spv ──spirv-cross──► …vert.msl
                                          (Vulkan)                       (Metal)
```

CMake's `koi_add_shader()` registers these commands; the `koi_shaders` target
builds the outputs into `build/shaders/`, beside the executable. At runtime
`loadShader` (via `selectShaderVariant`) loads whichever variant the active device
supports — `.msl` (entry `main0`) on Metal here. The pipeline is built once at
startup and bound each frame.

### Geometry: vertex & index buffers

Geometry lives in GPU memory, not in the shader (Step 1's approach) or the C++
heap. Shaders can only read GPU resources, and the fast memory a vertex buffer
wants is not CPU-writable, so getting data onto the GPU is a **two-step move**:

```
Vertex[] in CPU RAM ──memcpy──► transfer (staging) buffer ──copy pass──► vertex buffer (GPU)
```

`GpuRenderer::uploadToGpuBuffer()` encapsulates that move (create GPU buffer →
create + map an UPLOAD transfer buffer → memcpy → copy pass → submit) and is reused
for both the vertex buffer and the index buffer. As of Step 5 the public entry point
is `createMesh(vertices, indices)`, which runs that upload twice and wraps the two
buffers in a `Mesh`. This is the mirror image of `captureFrame`, which uses a
*download* transfer buffer to read pixels back. Uploads run when a mesh is created;
no fence is needed because command buffers execute in submission order, so the later
per-frame draw sees the finished copy.

The pipeline carries a **vertex input layout** — a buffer description (slot, pitch =
`sizeof(Vertex)`, per-vertex rate) plus one attribute per shader input (location,
format, byte offset). The attribute `location`s must match `triangle.vert`'s
`layout(location=N) in` declarations and `koi::Vertex`'s field offsets; that chain
(GLSL → SPIR-V → MSL `[[attribute(N)]]` → SDL attribute) is the contract the
`Vertex` `static_assert`s guard. The **index buffer** lets the cube's 12 triangles
reuse its 8 corner vertices (36 indices → 8 vertices) via `SDL_DrawGPUIndexedPrimitives`.

### Math, the MVP transform & the depth buffer (Step 3)

The cube lives in 3D **model space** and is carried to clip space by a
**Model-View-Projection** matrix. The math is hand-rolled (no GLM) in the
header-only `src/math/` module: `Mat4` is **column-major** (matching GLSL's
`mat4` memory layout, so it copies into the uniform with no transpose) and
`perspective()` produces depth in the **0…1** range SDL3's GPU API expects
(Metal/Vulkan/D3D), not OpenGL's −1…1. The View matrix is identity for now (a
real camera is Step 4); the camera distance is folded into the model translate.

The MVP is a **per-draw constant**, so it travels via a **uniform buffer** rather
than per-vertex attributes: `GpuRenderer::buildMvp()` builds it each frame (a
time-based spin) and `SDL_PushGPUVertexUniformData(cmd, 0, …)` hands it to the
vertex shader. SDL's SPIR-V convention puts vertex-stage uniform buffers in
descriptor set 1 (`layout(set = 1, binding = 0)`), and `loadShader` is told the
shader uses one uniform buffer.

A **depth buffer** makes the solid cube correct: a depth texture (sized to match
the swapchain, recreated on resize by `ensureDepthTexture`) is cleared to 1.0 each
frame and attached to the render pass; the pipeline's depth test (`compare_op =
LESS`, depth write on) keeps only the nearest fragment per pixel, so face draw
order no longer matters. The projection's aspect term also fixes Step 2's stretch.

### Camera, input & delta-time (Step 4)

The **View** in MVP (identity in Step 3) is now produced by a `Camera` in
`src/scene/`. It's a yaw/pitch fly camera: from two angles it derives a `forward`
vector and builds a view matrix with `Mat4::lookAt` — which transforms the world by
the camera's inverse (moving the camera is the same as moving the world the other
way). The camera sits *above* the renderer: `Engine` owns it, updates it from input,
and passes its `viewMatrix()` into `renderFrame`/`captureFrame`; the renderer stays
ignorant of cameras and input.

Input is split by nature. **Continuous** movement reads the live key array from
`SDL_GetKeyboardState` each frame (so held keys move smoothly), scaled by
**delta-time** (`SDL_GetTicksNS` deltas, clamped) so speed is frame-rate independent.
**Discrete** input stays event-driven in `processEvents`: quit/Escape, and mouse
motion under SDL **relative mouse mode** (cursor hidden + locked, motion as deltas)
feeding `addMouseLook`. Relative mode is enabled only for interactive runs, not the
headless `KOI_MAX_FRAMES`/`KOI_CAPTURE` paths.

### Meshes & the scene graph (Step 5)

Step 5 removes the last bit of hardcoding from the renderer. Geometry moved out of
`GpuRenderer` into reusable **`Mesh`** objects, and the flat cube list became a
**scene graph**: a tree of **`Node`**s, each with a local **`Transform`** (TRS) and
an optional shared `Mesh`.

```
            Mesh (renderer/)             scene graph (scene/)
   vertices+indices in GPU buffers   Node ── Transform (T·R·S) ── localMatrix()
   shared_ptr, made by createMesh     │└── shared_ptr<Mesh>   (may be null = group)
                                       └── unique_ptr children…  → a tree
```

Two passes per frame keep concerns separate: **(1)** `Node::updateWorldTransforms`
walks the tree top-down computing each node's world matrix as
`world = parentWorld · local` (so a parent's motion propagates to all descendants —
the whole point), then **(2)** `GpuRenderer::recordScene` walks it again and, for each
node that has a mesh, binds that mesh and draws at `proj·view·world`. Meshes are
`shared_ptr` (many nodes reuse one cube); children are `unique_ptr` (a node owns its
subtree). Layering note: `Node` (in `scene/`) references `Mesh` (in `renderer/`) — a
deliberate dependency, since a node that draws must name *what* geometry to draw;
`Camera` stays GPU-free. `Mat4` gained `scaling()` to support `Transform`.

> **Lifetime rule (RAII):** a `Mesh` frees its GPU buffers through the renderer's
> device, which it does *not* own. So every mesh must die before the device. The
> `Engine` enforces this by releasing the scene **before** the renderer in
> `shutdown()` (and by declaring `sceneRoot_` after `renderer_`, so default member
> destruction order agrees). Splitting `renderFrame` into `begin/draw/end` was
> considered and deliberately deferred — not needed yet.

### Textures & samplers (Step 6)

Surfaces are now painted by an **image** instead of only vertex colors. `Vertex`
gained a 2D **uv** (texture coordinate), so the cube split from 8 shared corners to
**24 vertices** — a per-face attribute (uv now, normals later) can't live on a shared
corner. The fragment shader samples a texture and multiplies by the vertex color
(now a tint): `outColor = texture(uTex, vUV) * vColor`.

Getting the image onto the GPU reuses the **staging move** from Step 2, in 2D:
`SDL_LoadBMP` → `SDL_ConvertSurface` (to RGBA32, matching `R8G8B8A8_UNORM`) →
`uploadToGpuTexture` (the sibling of `uploadToGpuBuffer`: map a transfer buffer,
copy, `SDL_UploadToGPUTexture`). It's the mirror of `captureFrame`'s pixel download.

```
   Texture (renderer/)            sampler (renderer-owned)        binding
   SDL_GPUTexture, RAII           LINEAR filter, REPEAT wrap      set=2, slot 0
   made by loadTexture            (how to read any texture)       BindGPUFragmentSamplers
```

A **sampler** (filtering between texels + wrap mode past the edges) is separate,
reusable device state, so the renderer owns one shared sampler and pairs it with
whatever `Texture` it draws. SDL3's resource sets are fixed: vertex uniform buffers
at `set=1` (the MVP), **fragment sampled textures at `set=2`**; `loadShader` now
declares the fragment shader's sampler count, and `recordScene` binds the
texture+sampler once before the traversal (one texture for the whole scene this
step — per-mesh materials are later). The image asset (`assets/uv_grid.bmp`) is
copied next to the binary by CMake's `koi_assets` target and loaded at runtime via
`SDL_GetBasePath()`, mirroring the shader pipeline. Same lifetime rule as meshes: the
`Texture` is released before the renderer's device in `shutdown()`.

### Lighting & normals (Step 7)

Surfaces now respond to light. `Vertex` gained a **normal** (the direction it faces);
the cube's 24-vertex split from Step 6 already lets each face carry its own flat
normal. Lighting is computed **in world space**, which forces one structural change:
the vertex shader needs the **model matrix on its own** (not just the combined MVP) to
output each fragment's world position + world normal. So the vertex uniform grew to
`{ mvp, model }`, pushed per node (`model` is the node's `worldMatrix()`). World normal
= `mat3(model) * normal` — correct for the scene's **uniform** scale; non-uniform scale
would need the inverse-transpose normal matrix (a noted future refinement).

```
   vertex shader (set=1)            fragment shader
   uniform { mvp, model }   ──►     ambient + diffuse(N·L) + specular(N·H)  over
   outputs world pos+normal         albedo = texture(set=2) · vColor
                                    light params: uniform LightUBO (set=3)
```

The fragment shader implements **Blinn-Phong**: ambient (constant fill) + diffuse
(`max(dot(N,L),0)`, a directional "sun") + specular (halfway vector `H=normalize(L+V)`,
needing the camera position), all modulating the textured albedo. The light is
frame-constant, so its parameters live in a **fragment uniform buffer at `set=3`**
(`loadShader` now declares the fragment shader's uniform-buffer count too), pushed once
per frame in `recordScene` via `SDL_PushGPUFragmentUniformData`. The camera position is
threaded through `renderFrame`/`captureFrame` (from `Camera::position()`) for the
specular term. Light parameters are constants for now — a per-object **material** is the
natural next refinement.

### Materials (Step 8)

Step 8 removes the "one global texture / hardcoded shininess" limitation so objects
can look different. A **`Material`** (in `scene/`) bundles a surface's appearance — a
`shared_ptr<Texture>` plus specular params — and a `Node` references one alongside its
mesh. This completes a clean three-way split: a drawn object is a **shape** (`Mesh`),
a **placement** (`Transform`/`Node`), and an **appearance** (`Material`), each varying
independently (the demo's hub and satellites share a texture but differ in shininess).

```
   Node ─┬─ Mesh        (shape)
         ├─ Transform   (placement)
         └─ Material ─→ Texture + shininess/specStrength   (appearance)
```

The binding model shifts accordingly: the texture, which Step 7 bound **once per
frame**, now binds **per draw** inside `recordNode` (different nodes use different
textures) — the same per-node rhythm already used for mesh buffers. The fragment
shader gains a **second** uniform buffer: light at `set=3,binding=0` (frame-constant,
pushed once in `recordScene`) and **material at `set=3,binding=1`** (per-draw, pushed
in `recordNode`); `loadShader` declares two fragment uniform buffers. The single
global `Engine::texture_` is gone — textures now live in materials owned by the scene,
so the existing "reset `sceneRoot_` before `renderer_`" teardown frees them in order.
The cost (rebinding per draw even when many objects share a material) is real;
batching/sorting by material is a deferred optimization.

### Models & shadows (Step 9)

Two features land together.

**Model loading.** `ModelLoader::loadModel` reads OBJ (**tinyobjloader**) or glTF
(**cgltf**) into a `Mesh`. These are single-header libraries downloaded at configure
time like `doctest`; the one TU that `#define`s their `*_IMPLEMENTATION` is compiled
with warnings off. OBJ indexes position/uv/normal separately, so the loader
de-duplicates each unique triple into one combined `Vertex`; glTF stores combined
vertices already. Real models can exceed 65 535 vertices, so `Mesh` now carries a
16- or 32-bit index element size and `createMesh` has a `Uint32` overload. The engine
gives each loaded model its own `Material` (geometry is read; file materials are not).

**Shadow mapping** is a two-pass technique:

```
   PASS 1  renderShadowPass:  scene depth → shadow map (depth-only pipeline,
           from the light's orthographic view; lightViewProj·world per node)
   PASS 2  recordScene:       main draw; the fragment shader projects worldPos by
           lightViewProj, samples the shadow map, compares depth (bias + 2x2 PCF)
           → occluded fragments lose diffuse + specular (keep ambient)
```

The renderer owns a sampleable depth **shadow map** (`DEPTH_STENCIL_TARGET | SAMPLER`),
a CLAMP **shadow sampler**, and a depth-only **shadow pipeline** (`shadow.vert` + an
empty `shadow.frag`, 0 color targets, with a depth bias to fight acne). `Mat4` gained
`orthographic` for the directional light's frustum; the light's view-projection is
computed once per frame and used by *both* passes (so they agree). The main fragment
shader gains a second sampler (the shadow map at `set=2,binding=1`) and `lightViewProj`
in the light UBO.

> **Toolchain note:** with two fragment samplers, `spirv-cross` ordered them by usage
> rather than by `layout(binding=)`, swapping them on Metal. Fixed by compiling MSL
> with **`--msl-decoration-binding`**, so resource indices follow the GLSL bindings —
> which is what SDL's slot-based binding expects.

### Post-processing (Step 10)

The scene stops rendering straight to the swapchain. Instead it renders into an engine-owned
**off-screen HDR target** (`R16G16B16A16_FLOAT`), and a chain of **fullscreen passes** then
processes that image before it reaches the screen. The HDR format is the enabling choice:
highlights can exceed 1.0, so tone-mapping has range to compress and bloom has genuine bright
areas to extract — an 8-bit target would clip both away.

```
  scene ─► sceneHDR ─► bloom bright-pass ─► separable blur (½-res, ping-pong)
                              │                        │
                              └──────────► composite ◄─┘  (scene + bloom, exposure,
                                              │            ACES tone-map, vignette, gamma)
                                              ▼
                                             ldr ─► FXAA ─► swapchain
```

Each pass draws a **fullscreen triangle** — three vertices synthesized from `gl_VertexIndex`
in `fullscreen.vert`, no vertex buffer, no depth — the standard buffer-free way to run a shader
over every pixel. Ordering is dictated by colour space: **bloom** reads linear HDR (before
tone-mapping); **composite** applies exposure, the ACES curve, the vignette, and the
**linear→sRGB gamma** encode; **FXAA** runs last on the final gamma-encoded LDR image because
it judges edges by *perceived* luma. The main pipeline's colour format changed from the
swapchain's to the HDR one; the off-screen targets are lazily (re)created to match the window
size by `ensureSceneTargets` (the same pattern the depth buffer already used). The whole
sequence lives in `renderSceneAndPost`, shared by `renderFrame` and `captureFrame` so the
headless BMP always matches the window. `PostSettings` (in `renderer/PostProcess.hpp`) carries
the per-effect toggles + exposure, driven from the keyboard in `Engine::processEvents`.

### Multiple lights (Step 11)

The single hardcoded directional "sun" becomes a **list** of lights of three kinds —
**directional** (parallel rays, no falloff), **point** (a position that fades with
distance), and **spot** (a point light confined to a cone). Light is **additive**, so
the fragment shader just **loops** over the active lights and sums each one's
Blinn-Phong contribution (ambient added once, up front). The vertex shader is
unchanged — it already emits world position + normal, all the loop needs.

```
   scene/Light.hpp (SDL-free)          shader (triangle.frag)
   Light{type,pos,dir,color,           for i in 0..lightCount:
     intensity,range,cone cos,           L,atten by type; Blinn-Phong;
     enabled}                            shadow only if i==0 & directional;
   + attenuation()/spotFactor()          result += radiance * (...)
   + activeLightCount()   ── mirror ──►  (helpers unit-tested headlessly)
```

Two new concepts arrive with point/spot lights. **Attenuation** — a point light's
brightness follows the inverse-square law (`1/(d²+1)`) multiplied by a range window
(`clamp(1−(d/range)⁴,0,1)²`) that forces an exact 0 at `range`, giving each light a
finite radius. **Spot cone** — the beam fades between an inner and outer half-angle;
we compare **cosines** (`dot(L, aim)` vs the cutoffs, via `smoothstep`) to skip an
`acos`, remembering that cosine *decreases* with angle (inner cutoff is the larger
cosine). Both are duplicated as pure CPU helpers in `Light.hpp` and unit-tested in
`tests/test_light.cpp` — the shader stays the runtime source of truth.

The lights travel to the shader as a **fixed-size `MAX_LIGHTS` array** in the light
UBO (`set=3,b=0`) — a uniform buffer's layout is fixed at pipeline-build time, so the
array can't be dynamic. Each `GpuLight` is packed as exactly **four `vec4`s** (64
bytes) so the C++ struct in `recordScene` and the GLSL struct agree byte-for-byte
under **std140** (a stray `vec3` would shift the whole array — the same alignment
class as Step 9's sampler-ordering bug). The `Engine` owns a `std::vector<Light>`
(like the camera + `PostSettings`), threads it through
`renderFrame`/`captureFrame` → `renderSceneAndPost` → `recordScene` as a
`std::span<const Light>`, animates one orbiting point light, and toggles light groups
from the keyboard (`5`/`6`/`7`); disabled lights are simply skipped when packing.

**Shadows stay on the sun only.** Each shadow-caster needs its own map (a point light
needs six — a cube map), so this step keeps the single Step-9 shadow map, driven by
`lights[0]`'s direction; the shader applies the shadow factor only to that light.
The fixed-array loop is also why "hundreds of lights" is a *different* architecture
(deferred / clustered shading) — a deliberate future milestone, not this step.

### PBR materials (Step 12)

The shading *inside* the Step 11 light loop switches from ad-hoc **Blinn-Phong** to a
physically-based **Cook-Torrance metallic-roughness** BRDF. This is a deliberately
contained change: no new vertex attributes, no new textures, no binding changes — the
`Material`'s two Blinn-Phong scalars (shininess/specStrength) simply become
`metallic`/`roughness`, pushed through the *same* material-uniform `vec4` lanes.

```
   scene/Material.hpp            shader (triangle.frag) per light:
   {texture, metallic,           F0   = mix(0.04, albedo, metallic)
    roughness}                   spec = D·G·F / (4·(N·V)(N·L))   (GGX/Smith/Fresnel)
   renderer/Pbr.hpp  ── mirror ─► kd   = (1-F)(1-metallic)        (metals: no diffuse)
   D/G/F helpers (unit-tested)   Lo  += shadow·(kd·albedo/π + spec)·radiance·(N·L)
```

The **specular** term is the microfacet BRDF: `D` (GGX/Trowbridge-Reitz) is how many
facets face the halfway vector — roughness widens this lobe; `G` (Smith with
Schlick-GGX) is microfacet self-shadowing; `F` (Fresnel-Schlick) is the grazing-angle
reflectance rise. The **metallic** parameter selects behaviour via the base
reflectance `F0`: dielectrics reflect a colourless ~4% and keep a diffuse albedo;
metals reflect their *own* albedo and have **no diffuse** (`kd` scaled by
`1-metallic`). **Energy conservation** weights diffuse by `1-F` and divides the
Lambertian term by `π` — which is why `setupLights`' intensities are larger than the
Blinn-Phong era (same brightness, honest math). The three D/G/F terms have pure CPU
twins in `renderer/Pbr.hpp`, unit-tested in `tests/test_pbr.cpp`.

The ambient term is a crude `ambient·albedo` fill: with only direct lights, **metals
look dark** away from their sharp reflections (a metal has no diffuse and no
environment to reflect). That is physically correct and is exactly what **image-based
lighting (IBL)** — a natural future step — would supply.

## Lifecycle & ownership

Startup builds bottom-up, shutdown tears down top-down — the standard RAII
ordering that keeps resources valid for exactly as long as something uses them:

```
init:      SDL_Init → Window → GpuRenderer (claims Window) → buildScene + loadTexture (upload meshes + image)
shutdown:  Scene + Texture (free GPU resources) → GpuRenderer (releases Window) → Window → SDL_Quit
```

Two ordering constraints, both enforced by `Engine` via smart-pointer resets:
`GpuRenderer` must be destroyed before `Window` (it holds the window's swapchain);
and the **scene and the texture** must be destroyed before `GpuRenderer` (each `Mesh`
and `Texture` frees its GPU resource through the renderer's device — see the lifetime
rule above). So `shutdown()` resets `sceneRoot_` and `texture_`, then `renderer_`,
then `window_`.

> Implementation note: `Engine`'s constructor and destructor are **defined in the
> `.cpp`**, not the header. Its `unique_ptr` members point to forward-declared
> types (`Window`, `GpuRenderer`); `unique_ptr`'s destructor needs the *complete*
> type, which only exists in the `.cpp`. This is the standard forward-declaration
> + `unique_ptr` pattern.

## Build system

CMake (≥ 3.28) with `CMakePresets.json` for one-command builds.

- `koi_core` — static library with all engine code. Building the engine as a
  library (rather than inlining it into `main.cpp`) lets the test runner link the
  **same** code the app runs.
- `koi-engine` — the application executable (`main.cpp` + `koi_core`), depends on
  `koi_shaders` so compiled shaders are always present.
- `koi-tests` — doctest runner (`koi_core` + the doctest header).
- `koi_warnings` — an INTERFACE target carrying strict warning flags, applied to
  our code only (not to SDL3 / doctest).
- `koi_shaders` — a custom target that compiles `shaders/*` to `build/shaders/`
  via `glslc` + `spirv-cross` (see "Shaders & the build-time toolchain" above).
- `koi_assets` — a custom target that copies `assets/*` (e.g. `uv_grid.bmp`) to
  `build/assets/` next to the binary, so textures are found at runtime via
  `SDL_GetBasePath()` (the same convention as the compiled shaders).

**Dependencies:** SDL3 via `find_package(SDL3 CONFIG)` (Homebrew ships the CMake
package). doctest is fetched as a single header at configure time (we download the
header directly rather than building doctest's CMake project, whose minimum
version is too old for CMake 4.x). Step 9's model loaders — **tinyobjloader** and
**cgltf** — are fetched the same way (single headers into `build/_deps/models`);
the one TU that implements them is compiled warning-free. Shader compilation needs
`glslc` (shaderc) and `spirv-cross` on `PATH` (`brew install shaderc spirv-cross`);
MSL is generated with `--msl-decoration-binding` so resource indices follow the GLSL
`binding=` decorations (matching SDL's slot-based binding).

Generator is **Unix Makefiles** (always available); install Ninja for faster
builds if you like.

## Conventions

- **Namespace `koi`** wraps all engine code.
- **Logging:** use `KOI_INFO/WARN/ERROR/DEBUG` (never raw `printf`/`std::cout`).
  After any SDL call that returns null/false, log `SDL_GetError()`.
- **Headers** include the SDL headers they need and use `#pragma once`; include
  paths are rooted at `src/` (e.g. `#include "core/Engine.hpp"`).
- **Testing hook:** `KOI_MAX_FRAMES` env var runs a fixed number of frames then
  exits cleanly — used for headless smoke tests.
- **Visual debugging:** `KOI_CAPTURE=<path.bmp>` renders one frame to an off-screen
  texture, downloads it, and saves a BMP, then exits (`GpuRenderer::captureFrame`).
  This is the preferred way to verify rendering output — no screen capture needed.
- **Shaders:** authored in GLSL under `shaders/`; never hand-write MSL/SPIR-V —
  the build generates those. Compiled output is loaded relative to the executable
  (`SDL_GetBasePath()` + `shaders/`).

## Planned layering (as features land)

Future milestones slot into this structure without reshaping it:

- **Step 2 (done):** `renderer/` grew GPU buffer/transfer helpers (`uploadToGpuBuffer`)
  and a `Vertex` type; geometry moved from the shader into vertex + index buffers with
  a described vertex layout.
- **Step 3 (done):** a header-only `math/` module (hand-rolled `Vec`, `Mat4`) feeds an
  MVP matrix to the shader via a uniform buffer; a depth buffer joined the render pass.
  (Quaternions were deferred — no use case yet — per the "introduce when needed"
  principle.)
- **Step 4 (done):** `scene/` arrived with a `Camera`; the identity View matrix
  became a real `lookAt` camera flown with keyboard/mouse, and a delta-time clock
  drives motion. `Mat4` gained `lookAt`. The renderer now takes a `view` and draws a
  hardcoded cube cluster via `recordScene`.
- **Step 5 (done):** geometry moved into reusable `Mesh` objects (made by the
  renderer's new `createMesh` factory; shapes in `Primitives`), and the hardcoded list
  became a **scene graph** — `scene/Transform` + `scene/Node` with parent/child world
  transforms. `Mat4` gained `scaling`. `renderFrame`/`captureFrame` now take a scene
  root and `recordScene` traverses it. (The `begin/draw/end` split was deferred.)
- **Step 6 (done):** `Vertex` grew a **uv**; a `Texture` type plus
  `loadTexture`/`uploadToGpuTexture` and a shared sampler joined `renderer/`; the fragment
  shader samples a texture (bound at `set=2`); CMake's `koi_assets` copies the image asset.
  The cube split to 24 vertices for per-face UVs.
- **Step 7 (done):** `Vertex` grew a **normal**; the vertex shader gained the `model`
  matrix (to light in world space) and the fragment shader a **Blinn-Phong** light at a
  fragment uniform buffer (`set=3`), pushed per frame with the camera position. Per-face
  normals fell out of the Step 6 24-vertex cube for free.
- **Step 8 (done):** a per-object **`Material`** (texture + specular params) replaced the
  single global texture; the fragment shader gained a second uniform buffer (material at
  `set=3,b=1`), bound per draw. Nodes now carry shape + placement + appearance.
- **Step 9 (done):** **model loading** (OBJ via tinyobjloader, glTF via cgltf → `Mesh`,
  with 32-bit indices) and **shadow mapping** (a depth pass from the light's orthographic
  view, sampled in the main pass with bias + PCF). `renderFrame` now runs two passes.
- **Step 10 (done):** **post-processing** — the scene renders into an offscreen **HDR** target,
  then a chain of **fullscreen passes** (bloom = bright-pass + separable blur; a composite that
  applies exposure + **ACES tone-mapping** + vignette + **gamma**; then **FXAA**) writes the
  final image. Added `renderer/PostProcess.hpp` (`PostSettings` + pure helpers), five shaders,
  and the shared `renderSceneAndPost` path; effects toggle at runtime.
- **Step 11 (done):** **multiple lights** — a header-only `scene/Light.hpp` (directional/point/
  spot + pure attenuation/spot-cone helpers) replaced the single hardcoded sun; the fragment
  shader loops over a fixed-size std140 light array, the `Engine` owns + animates + toggles a
  `std::vector<Light>` threaded through the render calls, and only the sun still casts a shadow.
- **Step 12 (done):** **PBR materials** — Blinn-Phong shading replaced by a Cook-Torrance
  metallic-roughness BRDF. `Material` now carries `metallic`/`roughness`; the fragment shader's
  per-light body uses GGX/Smith/Fresnel with energy conservation; pure BRDF helpers live in
  `renderer/Pbr.hpp` (unit-tested). No vertex/texture/binding changes — the material uniform
  lanes were repurposed.
- **Step 13+:** **texture maps** (per-pixel metallic/roughness + **normal maps**, which add a
  per-vertex tangent to `Vertex`), **image-based lighting (IBL)** for ambient + metal reflections,
  more shadow casters (sun cascades, point-light cube maps), and eventually deferred/clustered
  shading for many lights; glTF file materials/animation also open.
