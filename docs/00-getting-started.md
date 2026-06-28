# Getting Started

Welcome to **Koi Engine** — a 3D engine we build from scratch, one small step at a
time, to actually *understand* how real-time graphics works. This page gets the
project building and running on your machine. If you want to understand *what*
the code is doing and *why*, read [01 — Window & the Render Loop](01-window-and-render-loop.md)
next; it explains every graphics concept from first principles.

## Prerequisites

You need three things:

| Tool | What it is | Install (macOS) |
|------|-----------|-----------------|
| **SDL3** | Cross-platform library for windows, input, and the GPU | `brew install sdl3` |
| **CMake** ≥ 3.28 | Generates the build files our compiler uses | `brew install cmake` |
| A **C++20** compiler | Apple Clang (ships with Xcode Command Line Tools) | `xcode-select --install` |

> Optional: `brew install ninja` for faster builds. The presets use `make`
> (always available) so Ninja isn't required.

Verify SDL3 is discoverable:

```sh
pkg-config --modversion sdl3   # prints e.g. 3.4.8
```

## Build

The repo ships a `CMakePresets.json`, so building is two commands — no flags to
memorize:

```sh
cmake --preset debug      # configure: generates build files into build/
cmake --build build       # compile everything
```

This produces two executables in `build/`:

- `koi-engine` — the application (opens the window).
- `koi-tests`  — the test runner.

## Run

```sh
./build/koi-engine
```

A 1280×720 window opens, cleared to a calm dark teal every frame.

**Controls**

- `Esc` — quit
- Close button — quit
- Resize the window — handled automatically (logged in debug builds)

**Headless / automated runs.** Set `KOI_MAX_FRAMES` to render a fixed number of
frames and then exit cleanly — handy for smoke tests or CI where nobody is there
to press a key:

```sh
KOI_MAX_FRAMES=120 ./build/koi-engine
```

## Test

```sh
ctest --preset debug          # via CTest
# or run the binary directly:
./build/koi-tests
```

In Step 0 the test suite is small on purpose — it proves the test target links
against the engine and runs green. It fills out substantially in Step 3 when we
add the hand-rolled math library (vectors and matrices are perfect for unit
tests).

## Project layout

```
koi-engine/
├── CMakeLists.txt        # build description (targets, dependencies, warnings)
├── CMakePresets.json     # named build configs (debug/release)
├── docs/                 # these tutorials — read them alongside the code
├── src/
│   ├── main.cpp          # entry point — builds config, runs the Engine
│   ├── core/
│   │   ├── Log.hpp       # leveled logging (KOI_INFO/WARN/ERROR/DEBUG)
│   │   ├── Engine.*      # owns subsystems + the main loop
│   │   └── Window.*      # RAII wrapper around the OS window
│   └── renderer/
│       └── GpuRenderer.* # the GPU device + per-frame rendering
└── tests/
    └── test_core.cpp     # doctest-based tests
```

For the big-picture design and the *why* behind this structure, see
[ARCHITECTURE.md](../ARCHITECTURE.md).

## Where to next

➡️ [01 — Window & the Render Loop](01-window-and-render-loop.md): a from-scratch
explanation of GPUs, swapchains, command buffers, and render passes — and how the
Step 0 code maps onto them.
