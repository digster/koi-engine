# Koi Engine 🐟

A **3D engine built from scratch**, step by step, in modern **C++ (C++20)** on
top of **SDL3** and its cross-platform **GPU API** (Metal / Vulkan / Direct3D 12).

This is a *learning* project. The priority is understanding, not shipping a
product — so the code is heavily commented and every step has a matching tutorial
in [`docs/`](docs/index.html) that explains the underlying graphics concepts from
first principles, for readers new to graphics programming. The docs are plain HTML
(no build step) — open [`docs/index.html`](docs/index.html) in a browser.

> **Current status — Step 12:** adds **PBR materials**. The ad-hoc Blinn-Phong shading is replaced
> by the physically-based **Cook-Torrance metallic-roughness** model: every surface is described by
> two intuitive dials — **metallic** (metal vs. non-metal) and **roughness** (polished vs. matte) —
> that feed a microfacet BRDF (GGX distribution, Smith geometry, Fresnel) with proper **energy
> conservation**. Metals reflect their albedo and have no diffuse; dielectrics reflect a dim 4% over
> a matte body. (Metals look dark away from the highlights until **IBL** — a later step — gives them
> an environment to reflect.)
>
> **Controls:** `W`/`A`/`S`/`D` move, `E`/`Q` up/down, mouse to look, `Esc` to quit.
> Post-processing: `1` tone-map, `2` bloom, `3` FXAA, `4` vignette, `[` / `]` exposure.
> Lights: `5` point lights, `6` spot, `7` sun.

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
- [docs/05-camera-and-input.html](docs/05-camera-and-input.html) — the view matrix &
  `lookAt`, a yaw/pitch fly camera, delta-time, and events vs. polled input, mapped to
  the Step 4 code you fly with WASD + mouse.
- [docs/06-meshes-and-scene-graph.html](docs/06-meshes-and-scene-graph.html) — reusable
  meshes, the Transform (TRS) and matrix order, and a tree of nodes with parent/child
  transforms, mapped to the Step 5 code that animates a cube hierarchy.
- [docs/07-textures-and-samplers.html](docs/07-textures-and-samplers.html) — texture
  coordinates, uploading an image to the GPU, samplers (filtering & wrap), and the
  descriptor-set binding model, mapped to the Step 6 code that textures the scene.
- [docs/08-lighting-and-normals.html](docs/08-lighting-and-normals.html) — surface normals,
  a directional light, and the Phong terms (ambient/diffuse/specular) in world space,
  mapped to the Step 7 code that shades the textured scene.
- [docs/09-materials.html](docs/09-materials.html) — a per-object Material (texture +
  specular params), per-draw binding, and frame-vs-object uniforms, mapped to the Step 8
  code that gives each object its own look.
- [docs/10-models-and-shadows.html](docs/10-models-and-shadows.html) — loading OBJ/glTF
  models with single-header libraries, and shadow mapping (a depth pre-pass from the light),
  mapped to the Step 9 code that loads a sphere + torus and casts shadows.
- [docs/11-post-processing.html](docs/11-post-processing.html) — off-screen HDR targets, the
  fullscreen-triangle trick, tone-mapping & exposure, the gamma workflow, bloom, and FXAA,
  mapped to the Step 10 code that runs a fullscreen effect chain over the scene.
- [docs/12-multiple-lights.html](docs/12-multiple-lights.html) — directional/point/spot lights,
  distance attenuation, the spot cone, and accumulating many lights in a shader loop, mapped to
  the Step 11 code that lights the scene with a sun plus coloured point and spot lights.
- [docs/13-pbr-materials.html](docs/13-pbr-materials.html) — the Cook-Torrance metallic-roughness
  model: the microfacet BRDF (GGX, Smith, Fresnel), the metallic workflow, and energy conservation,
  mapped to the Step 12 code that swaps Blinn-Phong for physically-based shading.
- [ARCHITECTURE.md](ARCHITECTURE.md) — the big-picture design and the *why* behind it.

## Roadmap

| Step | Milestone | New concepts |
|------|-----------|--------------|
| ✅ **0** | **Window + clear screen** | GPU device, swapchain, command buffer, render pass |
| ✅ **1** | **First triangle** | Shaders, graphics pipeline, shader toolchain (`glslc` + `spirv-cross`) |
| ✅ **2** | **Vertex/index buffers** | GPU buffers, transfer buffers, vertex layouts |
| ✅ **3** | **3D cube + MVP + depth** | hand-rolled `vec`/`mat4`, projection, depth testing |
| ✅ **4** | **Camera + input movement** | view matrix, delta-time, fly camera |
| ✅ **5** | **Meshes & scene graph** | mesh abstraction, Transform (TRS), node hierarchy |
| ✅ **6** | **Textures** | UV coordinates, GPU textures, samplers (filtering & wrap) |
| ✅ **7** | **Phong lighting** | normals, a directional light, ambient/diffuse/specular |
| ✅ **8** | **Materials** | per-object texture + specular params, per-draw binding |
| ✅ **9** | **Models & shadows** | OBJ/glTF loading (tinyobjloader + cgltf), shadow mapping |
| ✅ **10** | **Post-processing** | offscreen HDR targets, fullscreen passes, tone-mapping, bloom, FXAA |
| ✅ **11** | **Multiple lights** | directional/point/spot lights, distance attenuation, spot cones |
| ✅ **12** | **PBR materials** | Cook-Torrance metallic-roughness BRDF (GGX + Smith + Fresnel), energy conservation |
| 13+ | Texture maps, IBL & more shadows | metallic-roughness/normal maps, image-based lighting, shadow cascades & point-light shadows |

## Requirements

- macOS (the current target; the GPU API is portable, so other platforms are
  feasible later)
- SDL3 ≥ 3.4, CMake ≥ 3.28, a C++20 compiler
- `glslc` (from `shaderc`) and `spirv-cross` for compiling shaders at build time

## License

[MIT](LICENSE) © digster
