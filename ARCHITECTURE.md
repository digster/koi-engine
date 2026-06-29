# Architecture

This document captures the **big-picture design** of Koi Engine — the parts you
can't see by reading a single file — and the reasoning behind the structural
choices. It evolves as the engine grows; right now it describes the foundation
through Step 6 (window & render loop, the first triangle, geometry in GPU
vertex/index buffers, a 3D cube with hand-rolled math + MVP uniform + depth
buffer, a fly camera driven by keyboard/mouse input with delta-time, a **mesh**
abstraction drawn through a **scene graph** of parent/child transforms, and
**textures** sampled onto those surfaces).

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
| `Engine` | [src/core/Engine.*](src/core/) | Owns subsystems + the camera + the scene graph, runs the game loop, computes delta-time, routes input, builds the demo scene (`buildScene`), animates + propagates transforms each frame, controls startup/shutdown order. |
| `Window` | [src/core/Window.*](src/core/) | RAII wrapper around one `SDL_Window`. Owns the OS window; the renderer attaches a swapchain to it. |
| `GpuRenderer` | [src/renderer/GpuRenderer.*](src/renderer/) | Owns the `SDL_GPUDevice`, the graphics pipeline, the depth texture, and one shared **sampler**. Is a **factory** for meshes (`createMesh`) and textures (`loadTexture`) and a **consumer** of a scene: per frame it takes a camera `view`, a scene root, and a texture, then records/submits the draw (acquire → ensure depth texture → render pass → `recordScene`: bind pipeline + texture/sampler once, then walk the tree binding each node's mesh + pushing `proj·view·world` + indexed draw → submit). Owns no geometry or texture data. |
| `Mesh` | [src/renderer/Mesh.*](src/renderer/) | RAII owner of one shape's vertex + index buffers (+ index count). Holds a **non-owning** device pointer used only to release those buffers — so every `Mesh` must be destroyed before the renderer's device. Created via `GpuRenderer::createMesh`; held by `shared_ptr` so many nodes share one. Non-copyable/non-movable. |
| `Texture` | [src/renderer/Texture.*](src/renderer/) | RAII owner of one `SDL_GPUTexture` (sampled image). Same non-owning-device lifetime rule as `Mesh`. Created via `GpuRenderer::loadTexture` (load BMP → convert → upload); held by `shared_ptr`. A *sampler* (how to read it) is separate, reusable device state owned by the renderer. |
| `Primitives` | [src/renderer/Primitives.*](src/renderer/) | Free functions (`makeCubeMesh`, `makePlaneMesh`) that define a shape's vertex/index data and upload it via `createMesh`, keeping the renderer geometry-agnostic. |
| `Transform` | [src/scene/Transform.hpp](src/scene/Transform.hpp) | Header-only, SDL-free: position / Euler rotation / scale, plus `localMatrix()` returning `T·R·S` (scale → rotate → translate). Unit-tested in `tests/test_transform.cpp`. |
| `Node` | [src/scene/Node.*](src/scene/) | One element of the scene graph: a local `Transform`, an optional shared `Mesh`, owned children (`unique_ptr`), and a cached world matrix. `updateWorldTransforms` walks the tree computing `world = parentWorld · local`. Scene-graph composition unit-tested in `tests/test_node.cpp`. |
| `Camera` | [src/scene/Camera.*](src/scene/) | A yaw/pitch fly camera (position + angles). Produces a `view` matrix via `lookAt`; `processKeyboard`/`addMouseLook` update it from input. Lives above the renderer and knows nothing about the GPU; its header is SDL-free. Logic unit-tested in `tests/test_camera.cpp`. |
| `Vertex` | [src/renderer/Vertex.hpp](src/renderer/Vertex.hpp) | The CPU-side layout of one vertex (3D position + color + uv). Its byte layout is the contract the pipeline's vertex attributes describe; pinned by `static_assert`s and `tests/test_vertex.cpp`. |
| `math` (`Vec`, `Mat4`) | [src/math/](src/math/) | Hand-rolled, header-only vector (`Vec2/3/4`) and column-major 4×4 matrix math (translate/rotate/**scale**/perspective/`lookAt`). No GLM — written ourselves so nothing stays a black box. Pure, so fully unit-tested in `tests/test_math.cpp`. |
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
renderFrame(view, → in GpuRenderer (view = camera.viewMatrix()):
  sceneRoot,           1. acquire command buffer
  texture)             2. acquire swapchain image (waits for vsync) + its size
                     3. ensure the depth texture matches that size
                     4. begin render pass (color CLEAR + depth CLEAR to 1.0)
                     5. recordScene: bind pipeline + texture/sampler once, then walk the
                        tree — per node with a mesh, bind its buffers + push proj·view·world
                        + indexed draw
                     6. end render pass
                     7. submit command buffer (queues present)
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
version is too old for CMake 4.x). Shader compilation needs `glslc` (shaderc) and
`spirv-cross` on `PATH` (`brew install shaderc spirv-cross`).

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
- **Step 7+:** `Vertex` grows **normals**; shaders gain per-fragment **lighting** (a
  directional light: ambient/diffuse/specular), pulling in a fragment uniform buffer and a
  normal matrix — the first shader change since this step. The 24-vertex cube already
  supports the per-face normals it needs.
