# Koi Engine 🐟

A **3D engine built from scratch**, step by step, in modern **C++ (C++20)** on
top of **SDL3** and its cross-platform **GPU API** (Metal / Vulkan / Direct3D 12).

This is a *learning* project. The priority is understanding, not shipping a
product — so the code is heavily commented and every step has a matching tutorial
in [`docs/tuts/`](docs/tuts/index.html) that explains the underlying graphics concepts from
first principles, for readers new to graphics programming. The docs are plain HTML
(no build step) — open [`docs/tuts/index.html`](docs/tuts/index.html) in a browser.

> **Current status — Step 24:** **instancing & draw-call sorting**. The engine now draws the same scene with
> **fewer, cheaper draw calls**. The flat render queue is **sorted** by batch key so identical work is adjacent,
> then **instanced** — a run of identical objects collapses into a *single* draw call (`num_instances = N`). The
> per-object transform moves out of a per-draw uniform into a **per-instance vertex buffer** the GPU walks itself
> (`triangle.vert`/`shadow.vert` gained `mat4` instance attributes; the uniform shrinks to the shared `viewProj`).
> The **colour pass** batches on `(material, mesh)`; the depth-only **shadow pass** batches on **mesh alone** (bigger
> runs). Pure `sortByMaterialMesh`/`coalesceBatches` helpers in [`RenderQueue`](src/renderer/RenderQueue.hpp) plan
> the batches (unit-tested); `GpuRenderer::buildFrameBatches` packs the instance buffers before the passes. The HUD
> shows a live **Draws N (M items)** readout — the batching win. The rendered image is unchanged. *(Known deferrals:
> CPU-side batching only — GPU-driven/indirect culling, deferred shading, and a render graph are future steps;
> transparent objects still draw one-per-call.)*
> *(Step 23 added **HUD & text** — an embedded 8×8 bitmap font baked into a texture atlas, drawn as a crisp
> screen-space overlay after post-processing.)*
>
> **Controls:** `W`/`A`/`S`/`D` move, `E`/`Q` up/down, mouse to look, `Esc` to quit.
> Post-processing: `1` tone-map, `2` bloom, `3` FXAA, `4` vignette, `[` / `]` exposure.
> Lights: `5` point lights, `6` spot, `7` sun. Environment: `8` skybox, `9` image-based lighting.
> Rendering: `0` frustum culling, `T` back-to-front transparency sort.
> Debug draw: `G` bounding boxes, `L` light icons, `F` freeze + show the camera frustum. HUD: `H` toggle.

## Quick start

```sh
brew install sdl3 cmake shaderc spirv-cross   # prerequisites (macOS)
cmake --preset debug                          # configure
cmake --build build                           # compile (also compiles shaders)
./build/koi-demo                              # run the sample app — Esc or close to quit
```

Full instructions, controls, and tests: [docs/tuts/00-getting-started.html](docs/tuts/00-getting-started.html).

## Why these choices?

- **SDL3 GPU API** (not OpenGL): OpenGL is deprecated on macOS (frozen at 4.1).
  The GPU API is modern, explicit, and cross-platform from a single codebase, so
  we learn how GPUs *actually* work today.
- **Hand-rolled math** (arriving in Step 3): we write our own vectors, matrices,
  and transforms so nothing stays a black box.
- **Heavily commented code + concept-first docs**: the code shows *how*; the
  [docs](docs/tuts/index.html) teach *why*.

## Documentation

- [docs/tuts/index.html](docs/tuts/index.html) — documentation home (open in a browser).
- [docs/tuts/00-getting-started.html](docs/tuts/00-getting-started.html) — build, run, test, layout.
- [docs/tuts/01-window-and-render-loop.html](docs/tuts/01-window-and-render-loop.html) — GPUs,
  swapchains, command buffers & render passes explained, mapped to the Step 0 code.
- [docs/tuts/02-first-triangle.html](docs/tuts/02-first-triangle.html) — shaders, the graphics
  pipeline, clip space, and the GLSL→SPIR-V→MSL toolchain, mapped to the Step 1 code.
- [docs/tuts/03-vertex-and-index-buffers.html](docs/tuts/03-vertex-and-index-buffers.html) — GPU
  vs CPU memory, transfer buffers & the copy pass, vertex input layouts, and index
  buffers, mapped to the Step 2 code that draws the gradient quad.
- [docs/tuts/04-3d-cube-mvp-and-depth.html](docs/tuts/04-3d-cube-mvp-and-depth.html) — the MVP
  transform chain, homogeneous coordinates, uniform buffers, and the depth buffer,
  mapped to the Step 3 code that spins a 3D cube.
- [docs/tuts/05-camera-and-input.html](docs/tuts/05-camera-and-input.html) — the view matrix &
  `lookAt`, a yaw/pitch fly camera, delta-time, and events vs. polled input, mapped to
  the Step 4 code you fly with WASD + mouse.
- [docs/tuts/06-meshes-and-scene-graph.html](docs/tuts/06-meshes-and-scene-graph.html) — reusable
  meshes, the Transform (TRS) and matrix order, and a tree of nodes with parent/child
  transforms, mapped to the Step 5 code that animates a cube hierarchy.
- [docs/tuts/07-textures-and-samplers.html](docs/tuts/07-textures-and-samplers.html) — texture
  coordinates, uploading an image to the GPU, samplers (filtering & wrap), and the
  descriptor-set binding model, mapped to the Step 6 code that textures the scene.
- [docs/tuts/08-lighting-and-normals.html](docs/tuts/08-lighting-and-normals.html) — surface normals,
  a directional light, and the Phong terms (ambient/diffuse/specular) in world space,
  mapped to the Step 7 code that shades the textured scene.
- [docs/tuts/09-materials.html](docs/tuts/09-materials.html) — a per-object Material (texture +
  specular params), per-draw binding, and frame-vs-object uniforms, mapped to the Step 8
  code that gives each object its own look.
- [docs/tuts/10-models-and-shadows.html](docs/tuts/10-models-and-shadows.html) — loading OBJ/glTF
  models with single-header libraries, and shadow mapping (a depth pre-pass from the light),
  mapped to the Step 9 code that loads a sphere + torus and casts shadows.
- [docs/tuts/11-post-processing.html](docs/tuts/11-post-processing.html) — off-screen HDR targets, the
  fullscreen-triangle trick, tone-mapping & exposure, the gamma workflow, bloom, and FXAA,
  mapped to the Step 10 code that runs a fullscreen effect chain over the scene.
- [docs/tuts/12-multiple-lights.html](docs/tuts/12-multiple-lights.html) — directional/point/spot lights,
  distance attenuation, the spot cone, and accumulating many lights in a shader loop, mapped to
  the Step 11 code that lights the scene with a sun plus coloured point and spot lights.
- [docs/tuts/13-pbr-materials.html](docs/tuts/13-pbr-materials.html) — the Cook-Torrance metallic-roughness
  model: the microfacet BRDF (GGX, Smith, Fresnel), the metallic workflow, and energy conservation,
  mapped to the Step 12 code that swaps Blinn-Phong for physically-based shading.
- [docs/tuts/14-texture-and-normal-maps.html](docs/tuts/14-texture-and-normal-maps.html) — driving material
  parameters per-pixel from maps (metallic-roughness, AO), tangent space and the TBN matrix behind
  normal mapping, and mipmaps + anisotropic filtering, mapped to the Step 13 code that adds surface
  relief and a per-pixel metal/dielectric checker.
- [docs/tuts/15-skybox-and-cubemaps.html](docs/tuts/15-skybox-and-cubemaps.html) — cubemaps sampled by
  direction, drawing a sky as a cube around the camera, the translation-stripped view and the
  far-plane depth trick that seats it behind everything, mapped to the Step 14 code that wraps the
  scene in a procedural day sky (the groundwork for IBL).
- [docs/tuts/16-image-based-lighting.html](docs/tuts/16-image-based-lighting.html) — lighting surfaces
  from the environment: the diffuse irradiance map, the specular split-sum (a prefiltered environment
  cube plus a BRDF LUT), importance sampling + the Hammersley sequence, and baking it by rendering into
  cube faces, mapped to the Step 15 code that finally makes metals reflect the sky.
- [docs/tuts/17-gltf-pbr-import.html](docs/tuts/17-gltf-pbr-import.html) — loading a real material
  from a file: what glTF is, image vs. texture vs. sampler, decoding embedded PNGs with stb_image, the
  sRGB-vs-linear gamma trap, and an emissive term that feeds bloom, mapped to the Step 16 code that
  imports the Damaged Helmet's full PBR material from its `.glb`.
- [docs/tuts/18-engine-app-separation.html](docs/tuts/18-engine-app-separation.html) — separating the
  reusable engine from the specific app: inversion of control and the `Application` hooks, one `FrameView`
  bundle of "what to draw", and the shutdown ordering that frees GPU resources safely, mapped to the Step 17
  code that lifts the demo out of the engine into a `samples/` app.
- [docs/tuts/19-quaternions.html](docs/tuts/19-quaternions.html) — storing rotation without
  gimbal lock: why Euler angles jam and can't be blended, the unit quaternion as an axis + half-angle, the
  sandwich product that rotates a vector, the Hamilton product that composes turns, and `slerp` for smooth
  interpolation, mapped to the Step 18 code that swaps `Transform` to a `Quat`.
- [docs/tuts/20-geometry-utilities.html](docs/tuts/20-geometry-utilities.html) — the spatial
  primitives an engine reuses everywhere: a `Mat4` inverse, an axis-aligned bounding box, a ray with the slab
  test, a plane's signed distance, and the camera frustum extracted from the view-projection matrix (with the
  z∈[0,1] near-plane caveat) — then spending the inverse on the **normal matrix** for correct lighting under
  non-uniform scale, mapped to the Step 19 code.
- [docs/tuts/21-render-queue-and-frustum-culling.html](docs/tuts/21-render-queue-and-frustum-culling.html) —
  the architecture pivot from *draw-while-you-walk* to **traverse → flat list → submit**: what a render queue is
  and why it unblocks culling/sorting/instancing, per-mesh bounding boxes, **frustum culling**, and the shadow-pass
  culling trap, mapped to the Step 20 code.
- [docs/tuts/22-transparency-and-alpha-blending.html](docs/tuts/22-transparency-and-alpha-blending.html) —
  what **alpha blending** is (the "over" operator), why it's order-dependent, and the **back-to-front sort** the
  render queue makes possible — plus a second blend pipeline (depth-write off) and the opaque → skybox →
  **transparent** draw order, mapped to the Step 21 code.
- [docs/tuts/23-debug-draw.html](docs/tuts/23-debug-draw.html) — **immediate-mode** line
  rendering: the **line-list** primitive vs. triangles, a **transient** per-frame vertex buffer, a depth-tested
  (write-off) overlay drawn into the HDR target, and recovering the **camera frustum**'s corners by unprojecting
  the **NDC cube** through `inverse(viewProj)` — the trick that finally makes Step 20's culling visible, mapped to
  the Step 22 code.
- [docs/tuts/24-hud-and-text.html](docs/tuts/24-hud-and-text.html) — **text on screen**: what a
  **bitmap font** and a **texture atlas** are, **screen-space** (pixel → clip) projection, and why UI is a separate
  **overlay pass in LDR** drawn *after* post-processing so glyphs stay crisp — plus the pure `Hud` collector and
  the half-texel **atlas-bleed** fix, mapped to the Step 23 code.
- [docs/tuts/25-instancing-and-draw-call-sorting.html](docs/tuts/25-instancing-and-draw-call-sorting.html)
  — **draw-call batching**: what a draw call costs (CPU submit + state changes), **sorting** by material to cut
  redundant binds, **instanced** rendering (one call, many copies) via per-**instance** vertex attributes and
  `gl_InstanceIndex`, and why the transform moves from a uniform to an instance buffer — mapped to the Step 24 code.
- [ARCHITECTURE.md](ARCHITECTURE.md) — the big-picture design and the *why* behind it.

## Roadmap

The engine grows one runnable milestone at a time. See **[ROADMAP.md](ROADMAP.md)** for the full
picture — every completed step (0–24) and the phased plan beyond: numbered next steps, then themed
tracks across rendering, animation, physics, audio, tooling, and gameplay.

## Requirements

- macOS (the current target; the GPU API is portable, so other platforms are
  feasible later)
- SDL3 ≥ 3.4, CMake ≥ 3.28, a C++20 compiler
- `glslc` (from `shaderc`) and `spirv-cross` for compiling shaders at build time

## License

[MIT](LICENSE) © digster
