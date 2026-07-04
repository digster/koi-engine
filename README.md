# Koi Engine 🐟

A **3D engine built from scratch**, step by step, in modern **C++ (C++20)** on
top of **SDL3** and its cross-platform **GPU API** (Metal / Vulkan / Direct3D 12).

This is a *learning* project. The priority is understanding, not shipping a
product — so the code is heavily commented and every step has a matching tutorial
in [`documentation/docs/`](documentation/docs/index.html) that explains the underlying graphics concepts from
first principles, for readers new to graphics programming. The docs are plain HTML
(no build step) — open [`documentation/docs/index.html`](documentation/docs/index.html) in a browser.

> **Current status — Step 19:** **geometry utilities + the normal matrix**. A new pure, header-only
> [`src/math/Geometry.hpp`](src/math/Geometry.hpp) adds the spatial primitives an engine reuses everywhere —
> **`Ray`**, an axis-aligned **`Aabb`**, a **`Plane`**, and the camera **`Frustum`** (extracted from the
> view-projection matrix, with the z∈[0,1] near-plane caveat) — plus a ray-box and frustum-box test, backed by
> the long-missing `Mat4` **`inverse`**/**`transpose`**. The step spends that inverse on a real fix: the vertex
> shader now carries normals by the **normal matrix** `transpose(inverse(model))`, so surfaces are lit
> correctly under **non-uniform scale**. It's pure math (fully unit-tested), the shared prerequisite for frustum
> culling, ray-cast picking, and physics; the demo scene is uniform-scale, so its frame is visually unchanged
> (only last-bit FP noise on a handful of edge pixels).
> *(Step 18 replaced Euler rotation in `Transform` with a hand-rolled unit **`Quat`** — no gimbal lock,
> `slerp`-able.)*
>
> **Controls:** `W`/`A`/`S`/`D` move, `E`/`Q` up/down, mouse to look, `Esc` to quit.
> Post-processing: `1` tone-map, `2` bloom, `3` FXAA, `4` vignette, `[` / `]` exposure.
> Lights: `5` point lights, `6` spot, `7` sun. Environment: `8` skybox, `9` image-based lighting.

## Quick start

```sh
brew install sdl3 cmake shaderc spirv-cross   # prerequisites (macOS)
cmake --preset debug                          # configure
cmake --build build                           # compile (also compiles shaders)
./build/koi-demo                              # run the sample app — Esc or close to quit
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
- [documentation/docs/15-skybox-and-cubemaps.html](documentation/docs/15-skybox-and-cubemaps.html) — cubemaps sampled by
  direction, drawing a sky as a cube around the camera, the translation-stripped view and the
  far-plane depth trick that seats it behind everything, mapped to the Step 14 code that wraps the
  scene in a procedural day sky (the groundwork for IBL).
- [documentation/docs/16-image-based-lighting.html](documentation/docs/16-image-based-lighting.html) — lighting surfaces
  from the environment: the diffuse irradiance map, the specular split-sum (a prefiltered environment
  cube plus a BRDF LUT), importance sampling + the Hammersley sequence, and baking it by rendering into
  cube faces, mapped to the Step 15 code that finally makes metals reflect the sky.
- [documentation/docs/17-gltf-pbr-import.html](documentation/docs/17-gltf-pbr-import.html) — loading a real material
  from a file: what glTF is, image vs. texture vs. sampler, decoding embedded PNGs with stb_image, the
  sRGB-vs-linear gamma trap, and an emissive term that feeds bloom, mapped to the Step 16 code that
  imports the Damaged Helmet's full PBR material from its `.glb`.
- [documentation/docs/18-engine-app-separation.html](documentation/docs/18-engine-app-separation.html) — separating the
  reusable engine from the specific app: inversion of control and the `Application` hooks, one `FrameView`
  bundle of "what to draw", and the shutdown ordering that frees GPU resources safely, mapped to the Step 17
  code that lifts the demo out of the engine into a `samples/` app.
- [documentation/docs/19-quaternions.html](documentation/docs/19-quaternions.html) — storing rotation without
  gimbal lock: why Euler angles jam and can't be blended, the unit quaternion as an axis + half-angle, the
  sandwich product that rotates a vector, the Hamilton product that composes turns, and `slerp` for smooth
  interpolation, mapped to the Step 18 code that swaps `Transform` to a `Quat`.
- [documentation/docs/20-geometry-utilities.html](documentation/docs/20-geometry-utilities.html) — the spatial
  primitives an engine reuses everywhere: a `Mat4` inverse, an axis-aligned bounding box, a ray with the slab
  test, a plane's signed distance, and the camera frustum extracted from the view-projection matrix (with the
  z∈[0,1] near-plane caveat) — then spending the inverse on the **normal matrix** for correct lighting under
  non-uniform scale, mapped to the Step 19 code.
- [ARCHITECTURE.md](ARCHITECTURE.md) — the big-picture design and the *why* behind it.

## Roadmap

The engine grows one runnable milestone at a time. See **[ROADMAP.md](ROADMAP.md)** for the full
picture — every completed step (0–18) and the phased plan beyond: numbered next steps, then themed
tracks across rendering, animation, physics, audio, tooling, and gameplay.

## Requirements

- macOS (the current target; the GPU API is portable, so other platforms are
  feasible later)
- SDL3 ≥ 3.4, CMake ≥ 3.28, a C++20 compiler
- `glslc` (from `shaderc`) and `spirv-cross` for compiling shaders at build time

## License

[MIT](LICENSE) © digster
