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
- **Step 16** (glTF PBR material import) shipped as
  [`documentation/docs/17-gltf-pbr-import.html`](documentation/docs/17-gltf-pbr-import.html); it loads a real
  production asset (the Khronos **Damaged Helmet**) with its material imported straight from the file — and
  brought **emissive** surfaces and **sRGB colour textures** along with it. With the material/lighting arc
  closed *and* now proven on real content, **the highest-leverage next moves are the production items** —
  engine/app separation, then cross-platform CI + golden images (see *Path to 1.0*) — interleaved with
  quaternions and transparency (see the suggested path).

---

## ✅ Completed — Steps 0–16

The forward-rendering fundamentals are done: from a blank window to physically-based, shadowed,
post-processed shading of loaded models under many lights, with per-pixel texture + normal maps,
a cubemap sky, and image-based lighting that lets the environment light the scene — now proven by
importing a real glTF PBR asset (the Damaged Helmet) with emissive surfaces and correct sRGB colour.
Each step has a concept-first tutorial —
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
| **13** | Texture & normal maps | per-pixel albedo/metallic-roughness/AO maps, tangent space + TBN matrix (normal mapping), mipmaps + anisotropic filtering | [documentation/docs/14](documentation/docs/14-texture-and-normal-maps.html) |
| **14** | Skybox & cubemaps | cubemap textures sampled by direction, cube-around-camera skybox, translation-stripped view, far-plane depth trick (LEQUAL + `.xyww`) | [documentation/docs/15](documentation/docs/15-skybox-and-cubemaps.html) |
| **15** | Image-based lighting | diffuse irradiance convolution, specular split-sum (prefiltered env + BRDF LUT), importance sampling + Hammersley, baking into cubemap faces | [documentation/docs/16](documentation/docs/16-image-based-lighting.html) |
| **16** | glTF PBR import | glTF material/texture import (base-colour/MR/normal/AO/**emissive**), PNG/JPG decode via stb_image, **sRGB** colour textures, emissive term feeding bloom, the Damaged Helmet | [documentation/docs/17](documentation/docs/17-gltf-pbr-import.html) |

> The prerequisites page [`documentation/docs/00-getting-started.html`](documentation/docs/00-getting-started.html) (building,
> running, testing, project layout) is not a numbered step.

---

## 🔜 Up next — the concrete milestones

The immediate path continues the physically-based rendering thread that Step 12 opened. These are
**numbered** because their order is fixed by dependency: IBL needs a cubemap, which the skybox
provides, and both build toward "metals that reflect their surroundings."

### ✅ Step 13 — Texture maps & normal mapping *(done — [tutorial](documentation/docs/14-texture-and-normal-maps.html))*
- **Shipped:** per-pixel material maps — a packed **metallic-roughness** map (glTF G=roughness,
  B=metallic), an **AO** map, and a tangent-space **normal map** — plus **mipmaps** and
  **anisotropic filtering**. A per-vertex `tangent` joined [`Vertex`](src/renderer/Vertex.hpp) (pure
  TBN math in [`src/renderer/Tangents.hpp`](src/renderer/Tangents.hpp), unit-tested);
  [`Material`](src/scene/Material.hpp) gained optional map handles with neutral 1×1 fallbacks (so
  map-less materials are unchanged — no shader permutation); the fragment shader reads five samplers
  and perturbs the normal via the TBN basis. Demo maps are generated by
  [`tools/gen_textures.py`](tools/gen_textures.py).
- **Deliberately deferred to a later step:** glTF PBR **material/texture import** + **PNG/JPG
  loading**, and therefore the **Damaged Helmet** verification (it needs both) — see *Full glTF PBR
  material import* under Geometry & content. This step verified with generated BMP maps on the demo
  meshes via `KOI_CAPTURE`.

### ✅ Step 14 — Skybox & environment maps *(done — [tutorial](documentation/docs/15-skybox-and-cubemaps.html))*
- **Shipped:** a **cubemap sky** drawn behind the scene. A new `uploadCubemap`/`loadCubemap` path in
  [`src/renderer/GpuRenderer.cpp`](src/renderer/GpuRenderer.cpp) uploads six BMP faces into one
  `SDL_GPU_TEXTURETYPE_CUBE` texture; a new skybox pipeline (`shaders/skybox.vert` /
  `shaders/skybox.frag`) draws the cube around the camera, sampling the cubemap by the cube's
  object-space direction. The chosen technique is the **cube-around-the-camera** one (reusing
  `makeCubeMesh`), with the view's **translation stripped** (sky stays infinitely far) and the depth
  **pinned to the far plane** (`.xyww` + `LEQUAL` + no depth write, drawn last) so it fills only
  background pixels. A dedicated **CLAMP_TO_EDGE** cubemap sampler avoids face seams. The demo sky is
  generated procedurally by [`tools/gen_skybox.py`](tools/gen_skybox.py) (a day-sky gradient + a sun
  aligned to the scene's sun light); toggle it at runtime with `8`.
- **Deliberately deferred:** the *fullscreen-triangle + inverse-view-projection* variant (needs a
  `Mat4` inverse the math library doesn't have yet); loading real HDR/equirectangular skies.
- **Why here:** the cubemap plumbing is the prerequisite for IBL — and a sky immediately makes every
  scene look better.

### ✅ Step 15 — Image-based lighting (IBL) *(done — [tutorial](documentation/docs/16-image-based-lighting.html))*
- **Shipped:** the environment now **lights** the scene, so **metals reflect their surroundings**
  instead of looking dark (the open problem from Step 12). Three maps are baked once from the skybox
  cubemap at load: a **diffuse irradiance** cube (the sky cosine-convolved), a **prefiltered specular**
  cube (GGX-blurred, roughness across its mips), and the environment-independent **BRDF LUT** — the
  **split-sum** approximation. The bakes render into cubemap faces via new pipelines +
  `createIblResources`/`bakeIbl` in [`src/renderer/GpuRenderer.cpp`](src/renderer/GpuRenderer.cpp)
  (reusing the Step 14 cubemap plumbing, so **no `Mat4` inverse** was needed — each face uses
  `lookAt`+90° `perspective`). New GLSL: `ibl_cube.vert`, `irradiance_convolution.frag`,
  `prefilter_env.frag`, `brdf_lut.frag`. [`shaders/triangle.frag`](shaders/triangle.frag) reads the
  three maps (fragment slots 5–7) for the ambient term, toggled at runtime with `9`. The precompute
  helpers (Hammersley, GGX importance sampling, the IBL geometry term, `integrateBRDF`,
  `fresnelSchlickRoughness`) got a CPU mirror in [`src/renderer/Pbr.hpp`](src/renderer/Pbr.hpp),
  unit-tested in [`tests/test_ibl.cpp`](tests/test_ibl.cpp).
- **Chosen approach:** **GPU precompute at startup** (render passes, not compute) and **full IBL**
  (diffuse + specular) in one step.
- **Deliberately deferred:** SDL3 GPU **compute**-based baking; loading real HDR/equirectangular
  environments (the demo bakes from the procedural BMP sky); a roughness-showcase row of spheres.
- **Why here:** it depended on Step 14's cubemaps and **closed out the core material/lighting arc** —
  from here the engine fans out into the wider tracks below.

### ✅ Step 16 — glTF PBR material import *(done — [tutorial](documentation/docs/17-gltf-pbr-import.html))*
- **Shipped:** materials are now **imported from glTF files**, not hand-authored. `loadModel` returns a
  `LoadedModel` (mesh **+** material); `loadGltf` reads `primitive.material` into
  [`Material`](src/scene/Material.hpp) — base-colour, metallic-roughness, normal, occlusion and **emissive**
  maps + the scalar factors. A new single-header dep, **stb_image** (fetched like cgltf), decodes the
  PNG/JPG images — embedded `.glb` `buffer_view`s decode straight from memory. Two correctness upgrades ride
  along: **sRGB** colour textures (base-colour/emissive upload as `*_SRGB` so the GPU un-gammas them; data
  maps stay linear — `uploadToGpuTexture` gained an `srgb` flag) and an **emissive** term
  (`Material.emissive`/`emissiveFactor`, a 9th fragment sampler + a second material-UBO `vec4`, added after
  shading in [`shaders/triangle.frag`](shaders/triangle.frag) so it also feeds the Step 10 bloom). The
  **Damaged Helmet** loads as the hero, verified via `KOI_CAPTURE`.
- **Deliberately deferred:** OBJ `.mtl` import; multi-primitive / multi-material meshes; base64 **data-URI**
  images; the full glTF **node hierarchy** (we import raw primitive geometry, so the app rotates the Z-up
  helmet upright itself); engine-wide colour management beyond colour textures; **Sponza**.
- **Why here:** it pays off the Step 13 IOU (that step built the per-pixel map machinery but fed it
  generated BMPs), and it puts every Step 12–15 subsystem to work on a single real asset.

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
- ✅ **Emissive materials** (glowing surfaces — gives the Step 10 bloom pass genuine bright sources,
  and it's one of the glTF PBR maps) — *done in Step 16* (`Material.emissive`, added post-shading).
- **Color management**: ✅ colour textures (base-colour/emissive) now sample as **sRGB** *(Step 16)*;
  the broader pipeline (render targets, tonemap, remaining shortcuts) is still open.
- **Reversed-Z** depth (float depth precision concentrated where it matters; small change, big
  z-fighting win on far planes).
- **Fog** — distance and height fog (small, classic, sells scale immediately).
- Depth of field, motion blur, richer color grading.

**Geometry & content**
- ✅ Full **glTF PBR material import** — read base-color / metallic-roughness / normal / AO / emissive
  from the file *(Step 16, via cgltf + stb_image)*. Still to do: honour glTF **samplers**,
  multi-primitive/multi-material meshes, and data-URI images.
- **Standard test scenes** — load the Khronos glTF samples: ✅ **Damaged Helmet** (materials) *loads as of
  Step 16*; **Sponza** (culling/lighting stress) still to come, as recurring verification + benchmarking.
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
Step 13  texture + normal maps (+ mipmaps)          ✅ done
   │
Step 14  skybox / cubemaps                           ✅ done
   │
Step 15  image-based lighting (IBL)                  ✅ done
   │
Step 16  glTF PBR material import (Damaged Helmet)   ✅ done
   │
engine/app separation ──▶ CI + golden images     ◀── next; highest production leverage, do early
   │
quaternions ──▶ transparency + blending ──▶ glTF node hierarchy / Sponza
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
