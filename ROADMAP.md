# Koi Engine — Roadmap 🐟

Koi is a **learning-focused 3D engine**, built one runnable milestone at a time. This file is
the single source of truth for where the engine has been and where it's going.

Three principles shape the plan:

- **Small steps, in order.** Each step is small, runnable, and *understood* before the next —
  so near-term work is numbered and concrete, while further-out work is grouped into themed
  **tracks** rather than over-committed to exact numbers.
- **A game engine is more than a renderer.** Rendering is one track among several. Animation,
  physics, audio, tooling, and gameplay are **first-class** here — not a "someday" pile.
- **Learning first, production eventually.** The long-term goal is a **professional-grade engine
  that real apps can build on**. That changes little about *what* we build, but a lot about *how*:
  a few items are **architecture pivots** (cheap to plan early, expensive to retrofit — see the
  pivots section below), the **Path to 1.0** track collects the production-hardening work, and a
  **Definition of 1.0** section states what "production-ready" actually means.

### How the numbering works

- The **Step N** number is the canonical milestone number: Step 0 is the first window on screen,
  Step 12 is physically-based materials.
- Tutorials live in [`documentation/docs/`](documentation/docs/index.html), but their filenames run **one ahead** of the step
  number, because [`documentation/docs/00-getting-started.html`](documentation/docs/00-getting-started.html) is a prerequisites
  page rather than a step. So **Step N → `documentation/docs/(N+1)-*.html`** (e.g. Step 12 →
  [`documentation/docs/13-pbr-materials.html`](documentation/docs/13-pbr-materials.html)).
- **The next milestone is Step 13**, which will ship as `documentation/docs/14-*.html`.

---

## ✅ Completed — Steps 0–12

The forward-rendering fundamentals are done: from a blank window to physically-based, shadowed,
post-processed shading of loaded models under many lights. Each step has a concept-first tutorial —
linked per row below (note how the doc number runs one ahead of the step), and all collected in
[`documentation/docs/index.html`](documentation/docs/index.html).

| Step | Milestone | Key concepts | Tutorial |
|------|-----------|--------------|----------|
| **0** | Window + clear screen | GPU device, swapchain, command buffer, render pass | [documentation/docs/01](documentation/docs/01-window-and-render-loop.html) |
| **1** | First triangle | shaders, graphics pipeline, shader toolchain (`glslc` + `spirv-cross`) | [documentation/docs/02](documentation/docs/02-first-triangle.html) |
| **2** | Vertex/index buffers | GPU buffers, transfer buffers, vertex layouts | [documentation/docs/03](documentation/docs/03-vertex-and-index-buffers.html) |
| **3** | 3D cube + MVP + depth | hand-rolled `vec`/`mat4`, projection, depth testing | [documentation/docs/04](documentation/docs/04-3d-cube-mvp-and-depth.html) |
| **4** | Camera + input movement | view matrix, delta-time, fly camera | [documentation/docs/05](documentation/docs/05-camera-and-input.html) |
| **5** | Meshes & scene graph | mesh abstraction, Transform (TRS), node hierarchy | [documentation/docs/06](documentation/docs/06-meshes-and-scene-graph.html) |
| **6** | Textures | UV coordinates, GPU textures, samplers (filtering & wrap) | [documentation/docs/07](documentation/docs/07-textures-and-samplers.html) |
| **7** | Phong lighting | normals, a directional light, ambient/diffuse/specular | [documentation/docs/08](documentation/docs/08-lighting-and-normals.html) |
| **8** | Materials | per-object texture + specular params, per-draw binding | [documentation/docs/09](documentation/docs/09-materials.html) |
| **9** | Models & shadows | OBJ/glTF loading (tinyobjloader + cgltf), shadow mapping | [documentation/docs/10](documentation/docs/10-models-and-shadows.html) |
| **10** | Post-processing | offscreen HDR targets, fullscreen passes, tone-mapping, bloom, FXAA | [documentation/docs/11](documentation/docs/11-post-processing.html) |
| **11** | Multiple lights | directional/point/spot lights, distance attenuation, spot cones | [documentation/docs/12](documentation/docs/12-multiple-lights.html) |
| **12** | PBR materials | Cook-Torrance metallic-roughness BRDF (GGX + Smith + Fresnel), energy conservation | [documentation/docs/13](documentation/docs/13-pbr-materials.html) |

> The prerequisites page [`documentation/docs/00-getting-started.html`](documentation/docs/00-getting-started.html) (building,
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
  matrix**; why a normal map needs a per-vertex tangent to orient it. Also **mipmaps** and
  **anisotropic filtering** — detail textures shimmer without a mip chain (today every texture is
  a single level, `num_levels = 1` in [`src/renderer/GpuRenderer.cpp`](src/renderer/GpuRenderer.cpp)),
  and this is the step where per-pixel sampling starts to dominate the image.
- **Likely touches:** add a `tangent` to [`src/renderer/Vertex.hpp`](src/renderer/Vertex.hpp);
  generate tangents in [`src/renderer/Primitives.cpp`](src/renderer/Primitives.cpp) and
  [`src/renderer/ModelLoader.cpp`](src/renderer/ModelLoader.cpp); build the TBN and sample the maps
  in [`shaders/triangle.vert`](shaders/triangle.vert) / [`shaders/triangle.frag`](shaders/triangle.frag);
  extend [`src/scene/Material.hpp`](src/scene/Material.hpp) with texture handles; generate mips at
  upload time (`SDL_GenerateMipmapsForGPUTexture`) and enable mip/aniso sampling on the shared sampler.
- **Verify with:** the Khronos glTF sample **Damaged Helmet** — the canonical PBR test model, it
  ships every map this step adds (albedo, metallic-roughness, normal, AO, emissive).
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

## ⚠️ Architecture pivots — decide early, build late

Most roadmap items are **additive**: skeletal animation or fog can land whenever, and nothing
else changes shape. A few are **pivots** — structural decisions whose retrofit cost grows with
every feature built on the old assumption. None needs to be *built* soon, but every new system
should be designed knowing they're coming:

- **Render-queue extraction.** Today [`GpuRenderer::recordScene`](src/renderer/GpuRenderer.cpp)
  walks the scene graph and draws inline. Splitting that into *traverse → flat draw list → submit*
  is the shared prerequisite of frustum culling, instancing, material sorting (a known cost since
  Step 8), transparency sorting, and deferred shading. It's the structural first move of the whole
  Scaling track.
- **Multithreading / job system.** Everything is single-threaded today. Asset loading, command
  recording, and simulation all eventually parallelize — and every system written before the job
  system bakes in single-threaded assumptions. Design new subsystems (asset manager, physics)
  with a "what if this ran on a worker thread?" eye.
- **Shader variant management.** [`shaders/triangle.frag`](shaders/triangle.frag) is a growing
  über-shader. Optional normal maps (Step 13), skinning, and deferred will force **permutations**
  (compile-time `#define` sets through the existing `glslc`/`spirv-cross` toolchain) plus pipeline
  caching. Plan the mechanism before the combinatorics arrive.
- **Memory strategy.** Every resource is a `shared_ptr` heap allocation. A production engine wants
  arenas/pools, per-frame scratch allocators, and streaming budgets. The natural moment to decide
  is the handle-based asset manager (Scene & data track).
- **Render graph.** Passes are hand-sequenced in `renderSceneAndPost` (shadow → scene → bloom →
  composite → FXAA). Add SSAO, SSR, and point-light shadows and the hand-wiring of targets and
  barriers stops scaling; a render graph (declare passes + dependencies, derive order and resource
  reuse) is both the fix and a great learning topic.

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
- **Render-queue extraction** (traverse → draw list → submit; *the* architecture pivot this whole
  track builds on — see the pivots section).
- Bounding volumes + **frustum culling** (skip what the camera can't see; needs the geometry
  utility layer from the Math track).
- **Instanced rendering** (one draw call for many copies).
- Sort the draw list by **pipeline / material** (kill the per-draw rebinding cost known since Step 8).
- **Deferred / clustered / tiled** shading (decouple lighting cost from geometry; scale to many
  lights — the known ceiling of forward shading, flagged in `ARCHITECTURE.md`).
- A **render graph** (declare passes + dependencies; derive order, targets, reuse — see pivots).
- GPU-driven culling via SDL3 GPU **compute** shaders; **occlusion culling** (Hi-Z) as a stretch.
- **SIMD** pass over the hand-rolled [`src/math/`](src/math/) once benchmarks exist to prove it.

**Transparency & blended effects** — the engine is **opaque-only** today: no pipeline has a blend
state, so glass, smoke, and foliage are all impossible.
- **Alpha blending** + back-to-front sorting (why transparent draws can't use the depth-write
  trick opaque ones do — a classic, and it needs the render queue to sort).
- **Alpha-tested cutout** (foliage, fences — the cheap half-way house that keeps depth writes).
- **Particle systems**: camera-facing **billboards**, additive/alpha blends; CPU-simulated first,
  then GPU **compute** (a natural second use of compute after Step 15's IBL bake).
- Order-independent transparency (OIT) as a stretch.

**Image quality**
- Anti-aliasing beyond FXAA: **MSAA**, then temporal AA (**TAA**).
- **SSAO** (ambient occlusion in screen space).
- **SSR** (screen-space reflections).
- **Emissive materials** (glowing surfaces — gives the Step 10 bloom pass genuine bright sources,
  and it's one of the glTF PBR maps).
- **Color management**: sample albedo as **sRGB** (today all textures are read as linear — a
  documented Step 10 shortcut that subtly skews PBR albedo).
- **Reversed-Z** depth (float depth precision concentrated where it matters; small change, big
  z-fighting win on far planes).
- **Fog** — distance and height fog (small, classic, sells scale immediately).
- Depth of field, motion blur, richer color grading.

**Geometry & content**
- Full **glTF PBR material import** (read base-color / metallic-roughness / normal / AO / emissive
  and samplers from the file — Steps 9 and 13 make this a small step).
- **Standard test scenes** — load the Khronos glTF samples (**Damaged Helmet** for materials,
  **Sponza** for culling/lighting stress) as recurring verification + benchmarking content.
- **Terrain**: heightmap-based, chunked; **noise** utilities (Perlin/simplex) to generate it — and
  to feed particles and procedural content later.
- Level-of-detail (LODs), mesh optimization, texture compression (KTX2 / Basis).

### Engine systems

**Math & transforms** — foundational; unblocks animation, culling, picking, and physics.
- **Quaternions** (replace Euler rotations; enable smooth `slerp`) — deliberately deferred until a
  real use case arrived. This is it.
- A **geometry utility layer** in [`src/math/`](src/math/): **AABB**, **ray**, **plane**, and
  **frustum** types with intersection tests. Unnamed until now, but it's the shared prerequisite
  of frustum culling, ray-cast picking, *and* the physics broadphase — one small, fully
  unit-testable module unblocks three tracks.
- Inverse-transpose normal matrix (correct normals under non-uniform scale — a known gap noted in
  `ARCHITECTURE.md`).

**Animation** *(needs quaternions)*
- Skeletal animation: vertex **skinning**, a joint hierarchy, glTF keyframe tracks.
- Animation blending and transitions; morph targets.

**Scene & data architecture**
- Grow the scene graph toward a component model / lightweight **ECS**.
- **Scene serialization** (save/load a scene to a file).
- A handle-based **asset manager** (dedupe + own meshes/textures/materials); asset hot-reload.
  This is also where the engine's **memory strategy** gets decided (handles vs `shared_ptr`,
  pools/arenas — see the pivots section).

**Physics & collision** *(needs the geometry utility layer; needs a fixed timestep)*
- A **fixed-timestep** simulation loop with render interpolation — deterministic integration is a
  *prerequisite* of stable physics, so it lands first (and the gameplay track's update tick reuses it).
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
- **Debug draw**: immediate-mode line/shape rendering (bounding boxes, normals, light icons,
  frusta) — the visual companion to culling, physics, and picking work across the other tracks.
- An immediate-mode **debug HUD** / overlay (stats, toggles).
- **Text rendering** (bitmap or SDF fonts) — nothing on screen is text yet.
- **Shader hot-reload** (watch [`shaders/`](shaders/), recompile through the existing
  `glslc`/`spirv-cross` toolchain, rebuild the pipeline live) — the single biggest edit-run loop win.
- A **config / CVar system**: engine tunables from a file + runtime tweaks (today's `KOI_*` env
  vars are the seed).
- An **editor**: scene hierarchy panel, property inspector, transform gizmos.
- A frame **profiler** / GPU-timing overlay.

**Gameplay & scripting**
- A game/update layer: objects with behaviors, an update tick distinct from render (built on the
  fixed timestep from the physics track).
- Optional scripting (e.g. Lua) or a C++ behavior system.
- A small **demo game** that exercises the whole stack end-to-end.

### Path to 1.0 — production hardening

The track that turns "my learning engine" into "an engine apps can ship on". It absorbs the old
*Platform & build* track and is deliberately **not** last-in-line — its first two items are the
highest-leverage things the project can do after Step 15.

- **Engine / app separation** — *the defining milestone for "production apps".* Today
  [`Engine::buildScene`](src/core/Engine.cpp) hardcodes the demo scene and `main.cpp` *is* the app.
  Move the demo into a `samples/` (or sandbox) app that consumes `koi_core` like any client would,
  and decide the **public API boundary**. Every step taken before this bakes more demo into the
  engine. Later: semantic versioning, deprecation policy, **API reference docs** (the tutorials
  teach concepts; consumers need reference).
- **Cross-platform CI + golden-image regression** — only macOS/Metal has ever executed; the Step 9
  `spirv-cross` sampler-swap bug is proof that backend-specific breakage is real and invisible to
  the compiler. A Linux/**Vulkan** CI build (lavapipe, the software driver) running the tests plus
  `KOI_CAPTURE` **golden-image comparisons** turns the existing capture tool into an automated net
  for exactly the "compiles clean, renders wrong" class of bug unit tests can't catch. Validate
  **D3D12**/Windows the same way when hardware allows.
- **Offline asset pipeline** — an *import → process → pack* cooking step (engine-native binary
  formats, so shipping apps never parse OBJ/glTF at runtime). Step 9's procedural model generation
  and the KTX2 plans above are seeds.
- **Robustness** — a defined failure policy (missing/corrupt asset, shader load failure, device
  lost) instead of ad-hoc logging; **fuzz the model loaders** (they parse untrusted files via
  tinyobjloader/cgltf — a production engine must not crash on malformed input).
- **Packaging** — installable SDK / app bundles; pin and vendor dependencies.

### Exploratory / stretch

Genuinely uncertain and likely far out — listed so they're on the radar, not committed:
- Global illumination: light probes / irradiance volumes, reflection probes, voxel or ray-traced GI.
- **Volumetrics**: light shafts / god rays, volumetric fog.
- **Decals** (projected surface details — bullet holes, splatters).
- GPU-driven everything (mesh shaders, bindless resources).
- Networking / multiplayer.
- VR / XR output.

---

## Suggested path

If you're unsure what to pick up next, a sensible interleaving that respects the dependencies:

```
Step 13  texture + normal maps (+ mipmaps)
   │
Step 14  skybox / cubemaps ──▶ Step 15  IBL
   │
engine/app separation ──▶ CI + golden images     ◀── highest production leverage; do early
   │
quaternions ──▶ glTF PBR import ──▶ transparency + blending
   │
geometry utils (AABB/ray/frustum) ──▶ render queue ──▶ frustum culling ──▶ cascaded shadows
   │
skeletal animation ──▶ debug draw / HUD / text ──▶ particles
   │
deferred / clustered ──▶ fixed timestep ──▶ physics + audio ──▶ editor + gameplay
```

Hard dependencies worth remembering: **tangents (Step 13) → normal mapping**, **skybox → IBL**,
**quaternions → skeletal animation**, **geometry utils → culling / picking / physics**,
**render queue → sorting / culling / instancing / transparency / worthwhile deferred**,
**fixed timestep → physics**. And keep one eye on the **architecture pivots** above — they're
cheap to respect and expensive to ignore.

---

## 🏁 Definition of 1.0

Feature lists don't define "production-ready" — **quality gates** do. Koi is 1.0 when all of
these hold, regardless of how many tracks above are finished:

- [ ] **Engine and app are separate**: real samples build against a documented public API, and
      nothing in `src/` hardcodes demo content.
- [ ] **CI is green on at least two backends** (Metal + Vulkan; D3D12 when hardware allows):
      every commit builds, passes the doctest suite, and runs headless with **zero GPU
      validation errors**.
- [ ] **Golden images are stable**: `KOI_CAPTURE` renders of the sample scenes match approved
      references on every CI run.
- [ ] **Loaders are fuzz-clean**: malformed OBJ/glTF input fails gracefully, never crashes.
- [ ] **Failure policy is defined and tested** for missing assets, shader load failures, and
      device loss.
- [ ] **Docs are two-tier**: concept tutorials (the existing `documentation/docs/`) *plus* API reference for
      the public surface; `ARCHITECTURE.md` current.
- [ ] **Versioned releases**: semantic versioning, a changelog, and a packaging story.

---

## How steps get added

The roadmap is also a reminder of *how* the project moves, so each milestone stays consistent with
the ones before it:

- **One small, runnable, understood step at a time.** Don't jump ahead; a step should build and be
  testable on its own before the next begins.
- **Ship the tutorial with the code.** Add a new `documentation/docs/NN-*.html` (mind the **+1 offset**: Step 13 →
  `documentation/docs/14-*.html`), following the existing pages' concept-first tone, and link it from
  [`documentation/docs/index.html`](documentation/docs/index.html). Code comments teach the *code*; docs teach the *concept*.
- **Keep the CPU/GPU mirror + tests.** Shader math gets a pure CPU twin in a header — as
  [`src/renderer/Pbr.hpp`](src/renderer/Pbr.hpp) and [`src/scene/Light.hpp`](src/scene/Light.hpp)
  mirror the shaders — so `tests/` (doctest) can validate it headlessly. Add tests with each feature
  and run them.
- **New shaders go through the toolchain.** Author once in GLSL under [`shaders/`](shaders/); the
  build compiles GLSL → SPIR-V (`glslc`) → MSL (`spirv-cross`). Remember the MSL entry point is
  `main0`, not `main`.
- **Verify visually without a window.** `KOI_CAPTURE=<path.bmp> ./build/koi-engine` renders one frame
  to a BMP (convert with `sips -s format png out.bmp --out out.png`) — the quickest way to confirm
  output. Once golden-image CI exists (Path to 1.0 track), a step that changes rendering also adds
  or refreshes its golden capture.
- **Keep the docs in sync.** Update `ARCHITECTURE.md` on structural changes and `README.md`'s status
  line; append the prompt to `PROMPT.md`; write a `memory/YYYY-MM-DD.md` summary; and generate a
  commit message (the project does not auto-commit).
