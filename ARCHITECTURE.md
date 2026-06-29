# Architecture

This document captures the **big-picture design** of Koi Engine — the parts you
can't see by reading a single file — and the reasoning behind the structural
choices. It evolves as the engine grows; right now it describes the foundation
through Step 2 (window & render loop, the first triangle, and geometry in GPU
vertex/index buffers).

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
| `Engine` | [src/core/Engine.*](src/core/) | Owns subsystems, runs the game loop, dispatches input events, controls startup/shutdown order. |
| `Window` | [src/core/Window.*](src/core/) | RAII wrapper around one `SDL_Window`. Owns the OS window; the renderer attaches a swapchain to it. |
| `GpuRenderer` | [src/renderer/GpuRenderer.*](src/renderer/) | Owns the `SDL_GPUDevice`, the graphics pipeline, and the geometry's vertex + index buffers; uploads geometry at startup (`uploadToGpuBuffer` + `createGeometry`) and records/submits one frame (acquire → render pass → `recordQuad` (bind pipeline + buffers, indexed draw) → submit). |
| `Vertex` | [src/renderer/Vertex.hpp](src/renderer/Vertex.hpp) | The CPU-side layout of one vertex (position + color). Its byte layout is the contract the pipeline's vertex attributes describe; pinned by `static_assert`s and `tests/test_vertex.cpp`. |
| `Shader` | [src/renderer/Shader.*](src/renderer/) | Loads a compiled shader for the active backend: `selectShaderVariant` (pure, unit-tested) picks the format/extension/entry point; `loadShader` reads the file and creates the `SDL_GPUShader`. |
| `Log` | [src/core/Log.hpp](src/core/Log.hpp) | Leveled logging macros over SDL's logger; one place to control verbosity. |

## Data flow: one frame

The render loop (in `Engine::run`) repeats:

```
processEvents()  → SDL_PollEvent drains input; Esc / close → stop the loop
       │
renderFrame()    → in GpuRenderer:
                     1. acquire command buffer
                     2. acquire swapchain image (waits for vsync)
                     3. begin render pass (load_op = CLEAR)
                     4. recordQuad: bind pipeline + vertex/index buffers, indexed draw
                     5. end render pass
                     6. submit command buffer (queues present)
```

See [docs/01-window-and-render-loop.html](docs/01-window-and-render-loop.html),
[docs/02-first-triangle.html](docs/02-first-triangle.html), and
[docs/03-vertex-and-index-buffers.html](docs/03-vertex-and-index-buffers.html) for
the concept-by-concept explanation.

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
`Vertex` `static_assert`s guard. The **index buffer** lets the quad's two triangles
reuse the 4 corner vertices (6 indices → 4 vertices) via `SDL_DrawGPUIndexedPrimitives`.

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
- **Step 3+:** a `math/` module (hand-rolled `vec`, `mat4`, `quat`) feeds MVP
  matrices to shaders via uniform buffers; a depth buffer joins the render pass.
  `renderFrame` likely splits into `begin/draw/end` once there are many draws.
- **Step 4+:** `scene/` (camera, transforms, node hierarchy) sits above the
  renderer and feeds it what to draw.
