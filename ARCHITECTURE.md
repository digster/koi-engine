# Architecture

This document captures the **big-picture design** of Koi Engine — the parts you
can't see by reading a single file — and the reasoning behind the structural
choices. It evolves as the engine grows; right now it describes the Step 0
foundation.

## Guiding principles

1. **Learn by building.** Every subsystem is introduced when a milestone needs
   it, never speculatively. Code is heavily commented; the [`docs/`](docs/)
   tutorials explain the underlying graphics *concepts* for newcomers.
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
| `GpuRenderer` | [src/renderer/GpuRenderer.*](src/renderer/) | Owns the `SDL_GPUDevice`; records and submits one frame (acquire → render pass → submit). |
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
                     4. [future: draw calls]
                     5. end render pass
                     6. submit command buffer (queues present)
```

See [docs/01-window-and-render-loop.md](docs/01-window-and-render-loop.md) for the
concept-by-concept explanation.

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
- `koi-engine` — the application executable (`main.cpp` + `koi_core`).
- `koi-tests` — doctest runner (`koi_core` + the doctest header).
- `koi_warnings` — an INTERFACE target carrying strict warning flags, applied to
  our code only (not to SDL3 / doctest).

**Dependencies:** SDL3 via `find_package(SDL3 CONFIG)` (Homebrew ships the CMake
package). doctest is fetched as a single header at configure time (we download the
header directly rather than building doctest's CMake project, whose minimum
version is too old for CMake 4.x).

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

## Planned layering (as features land)

Future milestones slot into this structure without reshaping it:

- **Step 1–2:** a `renderer/` grows shader loading, a `Pipeline` abstraction, and
  GPU buffer/transfer helpers. `renderFrame` splits into `begin/draw/end`.
- **Step 3+:** a `math/` module (hand-rolled `vec`, `mat4`, `quat`) feeds MVP
  matrices to shaders via uniform buffers; a depth buffer joins the render pass.
- **Step 4+:** `scene/` (camera, transforms, node hierarchy) sits above the
  renderer and feeds it what to draw.
