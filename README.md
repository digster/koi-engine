# Koi Engine 🐟

A **3D engine built from scratch**, step by step, in modern **C++ (C++20)** on
top of **SDL3** and its cross-platform **GPU API** (Metal / Vulkan / Direct3D 12).

This is a *learning* project. The priority is understanding, not shipping a
product — so the code is heavily commented and every step has a matching tutorial
in [`documentation/docs/`](documentation/docs/index.html) that explains the underlying graphics concepts from
first principles, for readers new to graphics programming. The docs are plain HTML
(no build step) — open [`documentation/docs/index.html`](documentation/docs/index.html) in a browser.

> **Current status — Step 13:** adds **texture &amp; normal maps**. Material parameters now vary
> **per pixel** from images: a packed **metallic-roughness** map (glTF convention: G = roughness,
> B = metallic), an **ambient-occlusion** map, and — the conceptually new part — a tangent-space
> **normal map** that adds fine surface relief without extra geometry, applied through a per-vertex
> **tangent** and the **TBN matrix**. Textures also gain **mipmaps** and **anisotropic filtering**,
> so tiled/receding surfaces no longer shimmer. Maps are optional: a material without one binds a
> neutral 1×1 fallback and renders exactly as it did in Step 12.
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

Full instructions, controls, and tests: [documentation/docs/00-getting-started.html](documentation/docs/00-getting-started.html).

## Why these choices?

- **SDL3 GPU API** (not OpenGL): OpenGL is deprecated on macOS (frozen at 4.1).
  The GPU API is modern, explicit, and cross-platform from a single codebase, so
  we learn how GPUs *actually* work today.
- **Hand-rolled math** (arriving in Step 3): we write our own vectors, matrices,
  and transforms so nothing stays a black box.
- **Heavily commented code + concept-first docs**: the code shows *how*; the
  [docs](documentation/docs/index.html) teach *why*.

## Documentation

- [documentation/docs/index.html](documentation/docs/index.html) — documentation home (open in a browser).
- [documentation/docs/00-getting-started.html](documentation/docs/00-getting-started.html) — build, run, test, layout.
- [documentation/docs/01-window-and-render-loop.html](documentation/docs/01-window-and-render-loop.html) — GPUs,
  swapchains, command buffers & render passes explained, mapped to the Step 0 code.
- [documentation/docs/02-first-triangle.html](documentation/docs/02-first-triangle.html) — shaders, the graphics
  pipeline, clip space, and the GLSL→SPIR-V→MSL toolchain, mapped to the Step 1 code.
- [documentation/docs/03-vertex-and-index-buffers.html](documentation/docs/03-vertex-and-index-buffers.html) — GPU
  vs CPU memory, transfer buffers & the copy pass, vertex input layouts, and index
  buffers, mapped to the Step 2 code that draws the gradient quad.
- [documentation/docs/04-3d-cube-mvp-and-depth.html](documentation/docs/04-3d-cube-mvp-and-depth.html) — the MVP
  transform chain, homogeneous coordinates, uniform buffers, and the depth buffer,
  mapped to the Step 3 code that spins a 3D cube.
- [documentation/docs/05-camera-and-input.html](documentation/docs/05-camera-and-input.html) — the view matrix &
  `lookAt`, a yaw/pitch fly camera, delta-time, and events vs. polled input, mapped to
  the Step 4 code you fly with WASD + mouse.
- [documentation/docs/06-meshes-and-scene-graph.html](documentation/docs/06-meshes-and-scene-graph.html) — reusable
  meshes, the Transform (TRS) and matrix order, and a tree of nodes with parent/child
  transforms, mapped to the Step 5 code that animates a cube hierarchy.
- [documentation/docs/07-textures-and-samplers.html](documentation/docs/07-textures-and-samplers.html) — texture
  coordinates, uploading an image to the GPU, samplers (filtering & wrap), and the
  descriptor-set binding model, mapped to the Step 6 code that textures the scene.
- [documentation/docs/08-lighting-and-normals.html](documentation/docs/08-lighting-and-normals.html) — surface normals,
  a directional light, and the Phong terms (ambient/diffuse/specular) in world space,
  mapped to the Step 7 code that shades the textured scene.
- [documentation/docs/09-materials.html](documentation/docs/09-materials.html) — a per-object Material (texture +
  specular params), per-draw binding, and frame-vs-object uniforms, mapped to the Step 8
  code that gives each object its own look.
- [documentation/docs/10-models-and-shadows.html](documentation/docs/10-models-and-shadows.html) — loading OBJ/glTF
  models with single-header libraries, and shadow mapping (a depth pre-pass from the light),
  mapped to the Step 9 code that loads a sphere + torus and casts shadows.
- [documentation/docs/11-post-processing.html](documentation/docs/11-post-processing.html) — off-screen HDR targets, the
  fullscreen-triangle trick, tone-mapping & exposure, the gamma workflow, bloom, and FXAA,
  mapped to the Step 10 code that runs a fullscreen effect chain over the scene.
- [documentation/docs/12-multiple-lights.html](documentation/docs/12-multiple-lights.html) — directional/point/spot lights,
  distance attenuation, the spot cone, and accumulating many lights in a shader loop, mapped to
  the Step 11 code that lights the scene with a sun plus coloured point and spot lights.
- [documentation/docs/13-pbr-materials.html](documentation/docs/13-pbr-materials.html) — the Cook-Torrance metallic-roughness
  model: the microfacet BRDF (GGX, Smith, Fresnel), the metallic workflow, and energy conservation,
  mapped to the Step 12 code that swaps Blinn-Phong for physically-based shading.
- [documentation/docs/14-texture-and-normal-maps.html](documentation/docs/14-texture-and-normal-maps.html) — driving material
  parameters per-pixel from maps (metallic-roughness, AO), tangent space and the TBN matrix behind
  normal mapping, and mipmaps + anisotropic filtering, mapped to the Step 13 code that adds surface
  relief and a per-pixel metal/dielectric checker.
- [ARCHITECTURE.md](ARCHITECTURE.md) — the big-picture design and the *why* behind it.

## Roadmap

The engine grows one runnable milestone at a time. See **[ROADMAP.md](ROADMAP.md)** for the full
picture — every completed step (0–12) and the phased plan beyond: numbered next steps, then themed
tracks across rendering, animation, physics, audio, tooling, and gameplay.

## Requirements

- macOS (the current target; the GPU API is portable, so other platforms are
  feasible later)
- SDL3 ≥ 3.4, CMake ≥ 3.28, a C++20 compiler
- `glslc` (from `shaderc`) and `spirv-cross` for compiling shaders at build time

## License

[MIT](LICENSE) © digster
