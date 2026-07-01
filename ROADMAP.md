# Koi Engine — Roadmap 🐟

Koi is a **learning-focused 3D engine**, built one runnable milestone at a time. This file is
the single source of truth for where the engine has been and where it's going.

Two principles shape the plan:

- **Small steps, in order.** Each step is small, runnable, and *understood* before the next —
  so near-term work is numbered and concrete, while further-out work is grouped into themed
  **tracks** rather than over-committed to exact numbers.
- **A game engine is more than a renderer.** Rendering is one track among several. Animation,
  physics, audio, tooling, and gameplay are **first-class** here — not a "someday" pile.

### How the numbering works

- The **Step N** number is the canonical milestone number: Step 0 is the first window on screen,
  Step 12 is physically-based materials.
- Tutorials live in [`docs/`](docs/index.html), but their filenames run **one ahead** of the step
  number, because [`docs/00-getting-started.html`](docs/00-getting-started.html) is a prerequisites
  page rather than a step. So **Step N → `docs/(N+1)-*.html`** (e.g. Step 12 →
  [`docs/13-pbr-materials.html`](docs/13-pbr-materials.html)).
- **The next milestone is Step 13**, which will ship as `docs/14-*.html`.

---

## ✅ Completed — Steps 0–12

The forward-rendering fundamentals are done: from a blank window to physically-based, shadowed,
post-processed shading of loaded models under many lights. Each step has a concept-first tutorial —
browse them from [`docs/index.html`](docs/index.html).

| Step | Milestone | Key concepts |
|------|-----------|--------------|
| **0** | Window + clear screen | GPU device, swapchain, command buffer, render pass |
| **1** | First triangle | shaders, graphics pipeline, shader toolchain (`glslc` + `spirv-cross`) |
| **2** | Vertex/index buffers | GPU buffers, transfer buffers, vertex layouts |
| **3** | 3D cube + MVP + depth | hand-rolled `vec`/`mat4`, projection, depth testing |
| **4** | Camera + input movement | view matrix, delta-time, fly camera |
| **5** | Meshes & scene graph | mesh abstraction, Transform (TRS), node hierarchy |
| **6** | Textures | UV coordinates, GPU textures, samplers (filtering & wrap) |
| **7** | Phong lighting | normals, a directional light, ambient/diffuse/specular |
| **8** | Materials | per-object texture + specular params, per-draw binding |
| **9** | Models & shadows | OBJ/glTF loading (tinyobjloader + cgltf), shadow mapping |
| **10** | Post-processing | offscreen HDR targets, fullscreen passes, tone-mapping, bloom, FXAA |
| **11** | Multiple lights | directional/point/spot lights, distance attenuation, spot cones |
| **12** | PBR materials | Cook-Torrance metallic-roughness BRDF (GGX + Smith + Fresnel), energy conservation |

> The prerequisites page [`docs/00-getting-started.html`](docs/00-getting-started.html) (building,
> running, testing, project layout) is not a numbered step.

---

## 🔜 Up next — the concrete milestones

The immediate path continues the physically-based rendering thread that Step 12 opened. These three
are **numbered** because their order is fixed by dependency: normal mapping needs a tangent, IBL
needs a cubemap, and both build toward "metals that reflect their surroundings."

### Step 13 — Texture maps & normal mapping
- **Goal:** drive material parameters per-*pixel* from textures, and add fine surface detail with
  normal maps.
- **Concepts:** albedo / metallic / roughness / AO texture maps; **tangent space** and the **TBN
  matrix**; why a normal map needs a per-vertex tangent to orient it.
- **Likely touches:** add a `tangent` to [`src/renderer/Vertex.hpp`](src/renderer/Vertex.hpp);
  generate tangents in [`src/renderer/Primitives.cpp`](src/renderer/Primitives.cpp) and
  [`src/renderer/ModelLoader.cpp`](src/renderer/ModelLoader.cpp); build the TBN and sample the maps
  in [`shaders/triangle.vert`](shaders/triangle.vert) / [`shaders/triangle.frag`](shaders/triangle.frag);
  extend [`src/scene/Material.hpp`](src/scene/Material.hpp) with texture handles.
- **Why here:** it's the smallest step off Step 12, and the tangent it introduces is a prerequisite
  for a lot of what follows.

### Step 14 — Skybox & environment maps
- **Goal:** render an environment — a cubemap sky — behind the scene.
- **Concepts:** **cubemap** textures and how they're sampled by *direction*; drawing the environment
  (a cube around the camera, or a fullscreen pass using the inverse view-projection); sampling at the
  far plane so the sky sits behind everything.
- **Likely touches:** cubemap support in [`src/renderer/Texture.hpp`](src/renderer/Texture.hpp) and
  the renderer; a new skybox pipeline in [`src/renderer/GpuRenderer.cpp`](src/renderer/GpuRenderer.cpp)
  with `shaders/skybox.vert` / `shaders/skybox.frag`.
- **Why here:** the cubemap plumbing is the prerequisite for IBL — and a sky immediately makes every
  scene look better.

### Step 15 — Image-based lighting (IBL)
- **Goal:** light surfaces from the environment so **metals stop looking dark** away from their
  highlights — the open problem left by Step 12.
- **Concepts:** a **diffuse irradiance** map (the environment convolved over the hemisphere); a
  **prefiltered specular** environment map across roughness levels; the **split-sum approximation**
  and the **BRDF integration LUT**; how environment reflection completes the PBR picture.
- **Likely touches:** one-time precompute passes (render or SDL3 GPU **compute**) to bake the
  irradiance map, prefiltered map, and BRDF LUT; sample them for the ambient term in
  [`shaders/triangle.frag`](shaders/triangle.frag); extend the CPU mirror in
  [`src/renderer/Pbr.hpp`](src/renderer/Pbr.hpp) for tests.
- **Why here:** it depends on Step 14's cubemaps and closes out the core material/lighting arc before
  the engine fans out into the wider tracks below.

---

## 🌱 The tracks — where the engine goes next

Beyond Step 15 the work fans out. These are **tracks**, not a single line: each advances at its own
pace and only occasionally depends on another (dependencies are noted inline). The engine-systems
tracks are first-class peers of the rendering ones.

### Rendering

**Shadows** — today only the sun casts shadows, from a single map.
- Cascaded shadow maps for the sun (split the view frustum → crisp near + covered far).
- Point-light shadows (omnidirectional cube-map depth).
- Spot-light shadows (a single perspective shadow map).
- Softer shadows (PCSS, larger PCF kernels).

**Scaling & performance** — today every node is drawn and every light is forward-accumulated.
- Bounding volumes + **frustum culling** (skip what the camera can't see).
- **Instanced rendering** (one draw call for many copies).
- **Deferred / clustered / tiled** shading (decouple lighting cost from geometry; scale to many
  lights — the known ceiling of forward shading, flagged in `ARCHITECTURE.md`).
- GPU-driven culling via SDL3 GPU **compute** shaders.

**Image quality**
- Anti-aliasing beyond FXAA: **MSAA**, then temporal AA (**TAA**).
- **SSAO** (ambient occlusion in screen space).
- **SSR** (screen-space reflections).
- Depth of field, motion blur, richer color grading.

**Geometry & content**
- Full **glTF PBR material import** (read base-color / metallic-roughness / normal / AO / emissive
  and samplers from the file — Steps 9 and 13 make this a small step).
- Level-of-detail (LODs), mesh optimization, texture compression (KTX2 / Basis).

### Engine systems

**Math & transforms** — foundational; unblocks animation.
- **Quaternions** (replace Euler rotations; enable smooth `slerp`) — deliberately deferred until a
  real use case arrived. This is it.
- Inverse-transpose normal matrix (correct normals under non-uniform scale — a known gap noted in
  `ARCHITECTURE.md`).

**Animation** *(needs quaternions)*
- Skeletal animation: vertex **skinning**, a joint hierarchy, glTF keyframe tracks.
- Animation blending and transitions; morph targets.

**Scene & data architecture**
- Grow the scene graph toward a component model / lightweight **ECS**.
- **Scene serialization** (save/load a scene to a file).
- A handle-based **asset manager** (dedupe + own meshes/textures/materials); asset hot-reload.

**Physics & collision**
- Broadphase (AABB sweep / BVH) + narrowphase intersection tests.
- Rigid-body dynamics; a character controller.
- Hand-rolled basics first (learning-first), with the option to integrate a library later.

**Audio**
- An audio mixer and sound playback on SDL's audio device.
- **3D spatial audio**: distance attenuation, stereo panning, doppler.

**Input & interaction**
- An input / **action-mapping** layer (rebindable, gamepad) beyond the built-in fly camera.
- Ray-cast **picking** (select the object under the cursor); manipulation gizmos.

**UI & tools**
- An immediate-mode **debug HUD** / overlay (stats, toggles).
- **Text rendering** (bitmap or SDF fonts) — nothing on screen is text yet.
- An **editor**: scene hierarchy panel, property inspector, transform gizmos.
- A frame **profiler** / GPU-timing overlay.

**Gameplay & scripting**
- A game/update layer: objects with behaviors, an update tick distinct from render.
- Optional scripting (e.g. Lua) or a C++ behavior system.
- A small **demo game** that exercises the whole stack end-to-end.

**Platform & build**
- Validate the **Vulkan / D3D12** backends on Windows & Linux (the GPU API is portable; only
  macOS/Metal is exercised today).
- Asset hot-reload; continuous integration; packaging.

### Exploratory / stretch

Genuinely uncertain and likely far out — listed so they're on the radar, not committed:
- Global illumination: light probes / irradiance volumes, reflection probes, voxel or ray-traced GI.
- GPU-driven everything (mesh shaders, bindless resources).
- Networking / multiplayer.
- VR / XR output.

---

## Suggested path

If you're unsure what to pick up next, a sensible interleaving that respects the dependencies:

```
Step 13  texture + normal maps
   │
Step 14  skybox / cubemaps ──▶ Step 15  IBL
   │
quaternions ──▶ cascaded shadows ──▶ glTF PBR import
   │
frustum culling ──▶ skeletal animation ──▶ debug UI / text
   │
deferred / clustered ──▶ physics + audio ──▶ editor + gameplay
```

Hard dependencies worth remembering: **tangents (Step 13) → normal mapping**, **skybox →
IBL**, **quaternions → skeletal animation**, **frustum culling → worthwhile deferred/clustered**.

---

## How steps get added

The roadmap is also a reminder of *how* the project moves, so each milestone stays consistent with
the ones before it:

- **One small, runnable, understood step at a time.** Don't jump ahead; a step should build and be
  testable on its own before the next begins.
- **Ship the tutorial with the code.** Add a new `docs/NN-*.html` (mind the **+1 offset**: Step 13 →
  `docs/14-*.html`), following the existing pages' concept-first tone, and link it from
  [`docs/index.html`](docs/index.html). Code comments teach the *code*; docs teach the *concept*.
- **Keep the CPU/GPU mirror + tests.** Shader math gets a pure CPU twin in a header — as
  [`src/renderer/Pbr.hpp`](src/renderer/Pbr.hpp) and [`src/scene/Light.hpp`](src/scene/Light.hpp)
  mirror the shaders — so `tests/` (doctest) can validate it headlessly. Add tests with each feature
  and run them.
- **New shaders go through the toolchain.** Author once in GLSL under [`shaders/`](shaders/); the
  build compiles GLSL → SPIR-V (`glslc`) → MSL (`spirv-cross`). Remember the MSL entry point is
  `main0`, not `main`.
- **Verify visually without a window.** `KOI_CAPTURE=<path.bmp> ./build/koi-engine` renders one frame
  to a BMP (convert with `sips -s format png out.bmp --out out.png`) — the quickest way to confirm
  output.
- **Keep the docs in sync.** Update `ARCHITECTURE.md` on structural changes and `README.md`'s status
  line; append the prompt to `PROMPT.md`; write a `memory/YYYY-MM-DD.md` summary; and generate a
  commit message (the project does not auto-commit).
