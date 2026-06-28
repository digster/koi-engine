# Koi Engine 🐟

A **3D engine built from scratch**, step by step, in modern **C++ (C++20)** on
top of **SDL3** and its cross-platform **GPU API** (Metal / Vulkan / Direct3D 12).

This is a *learning* project. The priority is understanding, not shipping a
product — so the code is heavily commented and every step has a matching tutorial
in [`docs/`](docs/index.html) that explains the underlying graphics concepts from
first principles, for readers new to graphics programming. The docs are plain HTML
(no build step) — open [`docs/index.html`](docs/index.html) in a browser.

> **Current status — Step 0:** opens a window and clears the screen to a solid
> color each frame through the SDL3 GPU API. This establishes the full GPU
> pipeline (device → swapchain → command buffer → render pass → submit) that
> every later feature builds on.

## Quick start

```sh
brew install sdl3 cmake      # prerequisites (macOS)
cmake --preset debug         # configure
cmake --build build          # compile
./build/koi-engine           # run — Esc or close the window to quit
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
- [ARCHITECTURE.md](ARCHITECTURE.md) — the big-picture design and the *why* behind it.

## Roadmap

| Step | Milestone | New concepts |
|------|-----------|--------------|
| ✅ **0** | **Window + clear screen** | GPU device, swapchain, command buffer, render pass |
| 1 | First triangle | Shaders, graphics pipeline, shader toolchain (`shadercross`) |
| 2 | Vertex/index buffers | GPU buffers, transfer buffers, vertex layouts |
| 3 | 3D cube + MVP + depth | hand-rolled `vec`/`mat4`, projection, depth testing |
| 4 | Camera + input movement | view matrix, delta-time, fly camera |
| 5 | Meshes & scene graph | model abstraction, node hierarchy |
| 6 | Textures + Phong lighting | samplers, normals, uniform buffers |
| 7+ | Models, shadows, post-fx | glTF loading, shadow maps, post-processing |

## Requirements

- macOS (the current target; the GPU API is portable, so other platforms are
  feasible later)
- SDL3 ≥ 3.4, CMake ≥ 3.28, a C++20 compiler

## License

[MIT](LICENSE) © digster
