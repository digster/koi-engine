# Architecture

This document captures the **big-picture design** of Koi Engine — the parts you
can't see by reading a single file — and the reasoning behind the structural
choices. It evolves as the engine grows; right now it describes the foundation
through Step 4 (window & render loop, the first triangle, geometry in GPU
vertex/index buffers, a 3D cube with hand-rolled math + MVP uniform + depth
buffer, and a fly camera driven by keyboard/mouse input with delta-time).

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
| `Engine` | [src/core/Engine.*](src/core/) | Owns subsystems + the camera, runs the game loop, computes delta-time, routes input (events for quit/look, polled key state for movement), controls startup/shutdown order. |
| `Window` | [src/core/Window.*](src/core/) | RAII wrapper around one `SDL_Window`. Owns the OS window; the renderer attaches a swapchain to it. |
| `GpuRenderer` | [src/renderer/GpuRenderer.*](src/renderer/) | Owns the `SDL_GPUDevice`, the graphics pipeline, the cube's vertex + index buffers, and the depth texture; uploads geometry at startup, and per frame takes a camera `view` matrix and records/submits the draw (acquire → ensure depth texture → render pass with color+depth targets → `recordScene`: bind once, then per-cube push `proj·view·model` + indexed draw → submit). |
| `Camera` | [src/scene/Camera.*](src/scene/) | A yaw/pitch fly camera (position + angles). Produces a `view` matrix via `lookAt`; `processKeyboard`/`addMouseLook` update it from input. Lives above the renderer and knows nothing about the GPU; its header is SDL-free. Logic unit-tested in `tests/test_camera.cpp`. |
| `Vertex` | [src/renderer/Vertex.hpp](src/renderer/Vertex.hpp) | The CPU-side layout of one vertex (3D position + color). Its byte layout is the contract the pipeline's vertex attributes describe; pinned by `static_assert`s and `tests/test_vertex.cpp`. |
| `math` (`Vec`, `Mat4`) | [src/math/](src/math/) | Hand-rolled, header-only vector (`Vec2/3/4`) and column-major 4×4 matrix math (translate/rotate/perspective/`lookAt`). No GLM — written ourselves so nothing stays a black box. Pure, so fully unit-tested in `tests/test_math.cpp`. |
| `Shader` | [src/renderer/Shader.*](src/renderer/) | Loads a compiled shader for the active backend: `selectShaderVariant` (pure, unit-tested) picks the format/extension/entry point; `loadShader` reads the file and creates the `SDL_GPUShader` (with the right uniform-buffer count). |
| `Log` | [src/core/Log.hpp](src/core/Log.hpp) | Leveled logging macros over SDL's logger; one place to control verbosity. |

## Data flow: one frame

The render loop (in `Engine::run`) repeats:

```
processEvents()  → SDL_PollEvent: Esc/close → stop; mouse motion → camera.addMouseLook
       │
dt + input       → dt = ΔSDL_GetTicksNS (clamped); camera.processKeyboard(polled keys, dt)
       │
renderFrame(view)→ in GpuRenderer (view = camera.viewMatrix()):
                     1. acquire command buffer
                     2. acquire swapchain image (waits for vsync) + its size
                     3. ensure the depth texture matches that size
                     4. begin render pass (color CLEAR + depth CLEAR to 1.0)
                     5. recordScene: bind once, then per cube push proj·view·model + indexed draw
                     6. end render pass
                     7. submit command buffer (queues present)
```

See [docs/01-window-and-render-loop.html](docs/01-window-and-render-loop.html),
[docs/02-first-triangle.html](docs/02-first-triangle.html),
[docs/03-vertex-and-index-buffers.html](docs/03-vertex-and-index-buffers.html),
[docs/04-3d-cube-mvp-and-depth.html](docs/04-3d-cube-mvp-and-depth.html), and
[docs/05-camera-and-input.html](docs/05-camera-and-input.html) for the
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
for both the vertex buffer and the index buffer in `createGeometry()`. This is the
mirror image of `captureFrame`, which uses a *download* transfer buffer to read
pixels back. The upload runs once at startup; no fence is needed because command
buffers execute in submission order, so the later per-frame draw sees the finished
copy.

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

The scene is a small **hardcoded list of cube positions**, drawn by `recordScene`
binding the geometry once and pushing a per-cube `proj·view·model` uniform before
each draw. This is deliberately *not* a scene graph — that abstraction is Step 5.

## Lifecycle & ownership

Startup builds bottom-up, shutdown tears down top-down — the standard RAII
ordering that keeps resources valid for exactly as long as something uses them:

```
init:      SDL_Init → Window → GpuRenderer (claims Window)
shutdown:  GpuRenderer (releases Window) → Window → SDL_Quit
```

`GpuRenderer` must be destroyed before `Window` because it holds the window's
swapchain. The `Engine` enforces this by storing them as `std::unique_ptr` and
resetting the renderer first.

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
- **Step 5+:** `scene/` grows a real **mesh** type and a **node hierarchy** (parent/
  child transforms) to replace the hardcoded cube list; `renderFrame` likely splits
  into `begin/draw/end` as the number and variety of draws grows.
