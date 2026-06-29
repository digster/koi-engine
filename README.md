# Koi Engine 🐟

A **3D engine built from scratch**, step by step, in modern **C++ (C++20)** on
top of **SDL3** and its cross-platform **GPU API** (Metal / Vulkan / Direct3D 12).

This is a *learning* project. The priority is understanding, not shipping a
product — so the code is heavily commented and every step has a matching tutorial
in [`docs/`](docs/index.html) that explains the underlying graphics concepts from
first principles, for readers new to graphics programming. The docs are plain HTML
(no build step) — open [`docs/index.html`](docs/index.html) in a browser.

> **Current status — Step 3:** draws a spinning **3D cube**. This adds the engine's
> own **hand-rolled math** (`Vec`/`Mat4`), a **Model-View-Projection** matrix fed to
> the vertex shader through a **uniform buffer**, and a **depth buffer** so near faces
> correctly hide far ones — the core 3D transform pipeline. (It also fixes Step 2's
> aspect-ratio stretch via the projection matrix.)

## Quick start

```sh
brew install sdl3 cmake shaderc spirv-cross   # prerequisites (macOS)
cmake --preset debug                          # configure
cmake --build build                           # compile (also compiles shaders)
./build/koi-engine                            # run — Esc or close to quit
```

Full instructions, controls, and tests: [docs/00-getting-started.html](docs/00-getting-started.html).

## Why these choices?

- **SDL3 GPU API** (not OpenGL): OpenGL is deprecated on macOS (frozen at 4.1).
  The GPU API is modern, explicit, and cross-platform from a single codebase, so
  we learn how GPUs *actually* work today.
- **Hand-rolled math** (arriving in Step 3): we write our own vectors, matrices,
  and transforms so nothing stays a black box.
- **Heavily commented code + concept-first docs**: the code shows *how*; the
  [docs](docs/index.html) teach *why*.

## Documentation

- [docs/index.html](docs/index.html) — documentation home (open in a browser).
- [docs/00-getting-started.html](docs/00-getting-started.html) — build, run, test, layout.
- [docs/01-window-and-render-loop.html](docs/01-window-and-render-loop.html) — GPUs,
  swapchains, command buffers & render passes explained, mapped to the Step 0 code.
- [docs/02-first-triangle.html](docs/02-first-triangle.html) — shaders, the graphics
  pipeline, clip space, and the GLSL→SPIR-V→MSL toolchain, mapped to the Step 1 code.
- [docs/03-vertex-and-index-buffers.html](docs/03-vertex-and-index-buffers.html) — GPU
  vs CPU memory, transfer buffers & the copy pass, vertex input layouts, and index
  buffers, mapped to the Step 2 code that draws the gradient quad.
- [docs/04-3d-cube-mvp-and-depth.html](docs/04-3d-cube-mvp-and-depth.html) — the MVP
  transform chain, homogeneous coordinates, uniform buffers, and the depth buffer,
  mapped to the Step 3 code that spins a 3D cube.
- [ARCHITECTURE.md](ARCHITECTURE.md) — the big-picture design and the *why* behind it.

## Roadmap

| Step | Milestone | New concepts |
|------|-----------|--------------|
| ✅ **0** | **Window + clear screen** | GPU device, swapchain, command buffer, render pass |
| ✅ **1** | **First triangle** | Shaders, graphics pipeline, shader toolchain (`glslc` + `spirv-cross`) |
| ✅ **2** | **Vertex/index buffers** | GPU buffers, transfer buffers, vertex layouts |
| ✅ **3** | **3D cube + MVP + depth** | hand-rolled `vec`/`mat4`, projection, depth testing |
| 4 | Camera + input movement | view matrix, delta-time, fly camera |
| 5 | Meshes & scene graph | model abstraction, node hierarchy |
| 6 | Textures + Phong lighting | samplers, normals, uniform buffers |
| 7+ | Models, shadows, post-fx | glTF loading, shadow maps, post-processing |

## Requirements

- macOS (the current target; the GPU API is portable, so other platforms are
  feasible later)
- SDL3 ≥ 3.4, CMake ≥ 3.28, a C++20 compiler
- `glslc` (from `shaderc`) and `spirv-cross` for compiling shaders at build time

## License

[MIT](LICENSE) © digster
