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
  brought **emissive** surfaces and **sRGB colour textures** along with it.
- **Step 17** (engine/app separation) shipped as
  [`documentation/docs/18-engine-app-separation.html`](documentation/docs/18-engine-app-separation.html); it
  lifted the demo out of the engine into a [`samples/demo/`](samples/demo) app behind a public
  **`koi::Application`** interface, so `koi_core` is now genuinely reusable.
- **Step 18** (quaternions) shipped as
  [`documentation/docs/19-quaternions.html`](documentation/docs/19-quaternions.html); `Transform` now stores a
  unit **`Quat`** instead of Euler angles — no gimbal lock, and rotations can be `slerp`-ed, the prerequisite for
  skeletal animation.
- **Step 19** (geometry utilities) shipped as
  [`documentation/docs/20-geometry-utilities.html`](documentation/docs/20-geometry-utilities.html); a pure
  `Ray`/`Aabb`/`Plane`/`Frustum` layer (plus the `Mat4` **inverse**) and the inverse-transpose **normal matrix**.
- **Step 20** (render queue + frustum culling) shipped as
  [`documentation/docs/21-render-queue-and-frustum-culling.html`](documentation/docs/21-render-queue-and-frustum-culling.html);
  the renderer stopped drawing inline while it walked the scene graph — it now flattens the tree into a flat
  **`RenderItem`** list (*traverse → list → submit*), the **architecture pivot** that unblocks sorting,
  instancing, and transparency, and spends the Step 19 `Aabb`/`Frustum` on its first payoff, **frustum culling**.
  With the material/lighting arc closed, the engine/app boundary drawn, rotation on animation-ready footing, and
  the render-queue pivot now made, the **learning tracks lead** from here — transparency, then sorting/instancing
  atop the new queue, then skeletal animation. **Cross-platform CI + golden images is now deliberately deferred**
  (still a 1.0 requirement — see *Path to 1.0* — but taken up *after* the other tracks, not early).

---

## ✅ Completed — Steps 0–22

The forward-rendering fundamentals are done: from a blank window to physically-based, shadowed,
post-processed shading of loaded models under many lights, with per-pixel texture + normal maps,
a cubemap sky, and image-based lighting that lets the environment light the scene — now proven by
importing a real glTF PBR asset (the Damaged Helmet) with emissive surfaces and correct sRGB colour,
then made reusable by separating the engine from the app (Step 17), put on animation-ready footing
by replacing Euler rotations with quaternions (Step 18), given a geometry layer (Step 19),
restructured around a **render queue** with **frustum culling** (Step 20 — the scaling-track pivot),
given see-through surfaces via **alpha blending** with a sorted back-to-front transparent pass (Step 21),
and given a **debug-draw** line overlay (AABBs, light icons, the camera frustum) that finally makes the
Step 19/20 geometry visible on screen (Step 22).
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
| **17** | Engine/app separation | public **`koi::Application`** interface (onStart/onUpdate/onEvent/frameView), **`FrameView`** render bundle, inversion of control, demo lifted from `src/` to `samples/demo/` (pixel-identical) | [documentation/docs/18](documentation/docs/18-engine-app-separation.html) |
| **18** | Quaternions | hand-rolled **`Quat`** (axis-angle, Hamilton product, sandwich rotate, `toMat4`, **`slerp`**), `Transform` stores a unit quaternion instead of Euler angles (no gimbal lock; interpolatable), camera kept on yaw/pitch by design | [documentation/docs/19](documentation/docs/19-quaternions.html) |

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
- **Deliberately deferred:** the *fullscreen-triangle + inverse-view-projection* variant (the `Mat4` inverse it
  needs now exists as of Step 19, but the variant itself is still unbuilt); loading real HDR/equirectangular skies.
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

### ✅ Step 17 — Engine / app separation *(done — [tutorial](documentation/docs/18-engine-app-separation.html))*
- **Shipped:** the demo scene that used to live *inside* the engine is now a standalone **sample app**.
  `koi_core` keeps the reusable machinery — window, [`GpuRenderer`](src/renderer/GpuRenderer.cpp), and the
  main loop (plus the headless `KOI_CAPTURE`/`KOI_MAX_FRAMES` paths) — and drives a small public
  **`koi::Application`** interface ([`src/core/Application.hpp`](src/core/Application.hpp):
  `onStart`/`onUpdate`/`onEvent`/`frameView`/`onShutdown`) by **inversion of control**. All content and
  behaviour (scene, camera, lights, animation, input) moved to [`samples/demo/`](samples/demo) as
  `DemoApp : koi::Application`, so **nothing in `src/` hardcodes demo content**. A single
  **[`FrameView`](src/renderer/FrameView.hpp)** bundle carries "what to draw" across the boundary and unifies
  the live (`renderFrame`) and capture (`captureFrame`) paths. The executable is now `koi-demo` (built from
  `samples/demo/`). Verified behaviour-preserving: the capture is **byte-identical** to Step 16.
- **Deliberately deferred:** semantic versioning + a deprecation policy; a two-tier **API reference** doc
  (the tutorials teach concepts, consumers still need reference); multiple sample apps; a formal
  services/facade beyond `Engine::renderer()`.
- **Why here:** it's the *defining* production milestone (see *Path to 1.0*) — until the demo lifts out
  cleanly, the engine can't be built on. Doing it now, before more systems land, stops each new subsystem
  from baking more demo into the engine.

### ✅ Step 18 — Quaternions *(done — [tutorial](documentation/docs/19-quaternions.html))*
- **Shipped:** a hand-rolled unit quaternion, [`src/math/Quat.hpp`](src/math/Quat.hpp) (header-only, glTF
  `x,y,z,w` order): `fromAxisAngle`/`fromEuler`, the Hamilton product (`operator*`), the sandwich-product
  `rotate`, `conjugate`/`inverse`, `toMat4`, and **`slerp`** (shortest-arc, with double-cover sign flip and a
  near-parallel nlerp fallback). [`Transform`](src/scene/Transform.hpp) now stores a `Quat rotation` in place
  of `Vec3 rotationEuler`, so orientations can no longer gimbal-lock and can be interpolated. Because
  `Quat::fromEuler` reproduces the old `Rz·Ry·Rx` composition, the swap was **behaviour-preserving** (the
  `KOI_CAPTURE` frame is visually identical — a few edge/highlight pixels differ only in their last FP bits).
  Unit-tested in [`tests/test_quat.cpp`](tests/test_quat.cpp).
- **Deliberately deferred:** a `Mat4` inverse (built next, in Step 19); converting the camera to quaternions (its clamped yaw/pitch is
  the correct control scheme — no roll — so it stays Euler by design); and wiring `slerp` into the demo (it's
  implemented and tested, but the visible payoff waits for skeletal animation).
- **Why here:** it's the small, self-contained math prerequisite that unblocks the **skeletal animation** track
  (per-joint rotations stored as quaternions, `slerp`-ed between keyframes).

### ✅ Step 19 — Geometry utilities + normal matrix *(done — [tutorial](documentation/docs/20-geometry-utilities.html))*
- **Shipped:** the long-deferred `Mat4` **`inverse`** + **`transpose`** ([`src/math/Mat4.hpp`](src/math/Mat4.hpp))
  and a new pure, header-only geometry layer [`src/math/Geometry.hpp`](src/math/Geometry.hpp): **`Ray`**,
  **`Aabb`** (center/extents/contains/expand/merge/`transformed`), **`Plane`** (signed distance), and
  **`Frustum`** — Gribb–Hartmann plane extraction adapted to our **z ∈ [0,1]** clip convention (near plane is
  `row2` *alone*, not `row3+row2`) plus a conservative positive-vertex AABB test — with a slab-based
  `intersect(ray, box)`. The step's live payoff spends the new inverse on the **normal matrix**: the vertex
  shader now carries normals by `transpose(inverse(model))`
  ([`shaders/triangle.vert`](shaders/triangle.vert), uploaded per-draw from
  [`GpuRenderer.cpp`](src/renderer/GpuRenderer.cpp)), fixing lighting under **non-uniform scale** (the tangent
  correctly stays on `model`). Unit-tested in [`tests/test_geometry.cpp`](tests/test_geometry.cpp). The demo
  scene is uniform-scale, so its `KOI_CAPTURE` frame is **visually unchanged** — not byte-identical, but only
  ~240 edge/highlight bytes differ in their last FP bits (max ≈ 23/255), the same rebuild noise as Step 18.
- **Deliberately deferred:** the **render-queue** extraction and *actually* skipping culled draws (frustum
  culling) — *done next, in Step 20*; ray-cast **picking** UI (unprojecting the cursor); ray–plane / ray–sphere tests.
- **Why here:** one small, pure, fully testable layer that is the shared prerequisite of **frustum culling**,
  **ray-cast picking**, and the **physics broadphase** — build the math before the systems that consume it.

### ✅ Step 20 — Render queue + frustum culling *(done — [tutorial](documentation/docs/21-render-queue-and-frustum-culling.html))*
- **Shipped:** the **render-queue extraction** (the architecture pivot) + its first payoff, **frustum culling**.
  A new [`src/renderer/RenderQueue.hpp`](src/renderer/RenderQueue.hpp)/`.cpp` introduces a flat **`RenderItem`**
  list and three functions: `computeLocalBounds` (a mesh's model-space `Aabb`, folded from its vertices),
  `buildRenderQueue` (walk the tree once → flat list) and `cullToFrustum` (the visibility filter reusing Step 19's
  `Frustum::intersectsAabb`). [`Mesh`](src/renderer/Mesh.hpp) now stores its local `Aabb` (computed in
  `createMesh`). [`GpuRenderer`](src/renderer/GpuRenderer.cpp) builds the queue once per frame in
  `renderSceneAndPost`; the recursive `recordNode`/`recordShadowNode` became a per-item `submitItem` + shadow
  loop. The **camera pass** is frustum-culled (a runtime toggle, key **`0`**, + a logged drawn/total count); the
  **shadow pass stays unculled** (an off-screen caster can still cast an in-view shadow — the culling trap).
  Unit-tested in [`tests/test_render_queue.cpp`](tests/test_render_queue.cpp). Pure restructuring (no math
  changed), so the `KOI_CAPTURE` frame is **byte-for-byte identical** to Step 19.
- **Deliberately deferred:** **sorting** the queue by pipeline/material; **instancing**; culling the *shadow* pass
  to the light frustum; ray-cast **picking**. Transparency (which needs the queue to sort) is the next track.
- **Why here:** the render queue is *the* structural first move of the whole scaling track — culling, sorting,
  instancing, transparency and deferred all operate on the flat list, none expressible while draw ≡ traverse.

### ✅ Step 21 — Transparency + alpha blending *(done — [tutorial](documentation/docs/22-transparency-and-alpha-blending.html))*
- **Shipped:** the engine's **first translucent surfaces**. [`Material`](src/scene/Material.hpp) gains an
  **`AlphaMode`** (`Opaque`/`Blend`) + **`opacity`**; [`triangle.frag`](shaders/triangle.frag) now emits a real
  alpha (`material.w × albedo.a`) instead of a hardcoded `1.0`. [`GpuRenderer`](src/renderer/GpuRenderer.cpp)
  builds a **second scene pipeline** — same shaders, but **blending on** (the "over" operator) and **depth-write
  off** (depth *test* stays) — and `recordScene` now runs **opaque → skybox → transparent**, the sky moving before
  the transparent pass so glass blends over the resolved background. `partitionByBlend`
  ([`RenderQueue.cpp`](src/renderer/RenderQueue.cpp)) splits the culled list and sorts the transparent items
  **back-to-front** by camera distance; a runtime toggle (key **`T`**) turns the sort off to expose the artifact.
  The demo gains two overlapping glass panes; unit-tested in
  [`tests/test_render_queue.cpp`](tests/test_render_queue.cpp).
- **Deliberately deferred:** **alpha-tested cutout** (glTF `MASK`); translucent objects still cast **solid
  shadows** (the shadow pass ignores materials); the per-object sort **mis-orders interpenetrating** meshes (→ OIT);
  the glTF loader still imports every model **opaque** (`alphaMode`/`baseColorFactor.a` unread).
- **Why here:** blending is order-dependent, so it's the first feature that *had* to have the Step 20 render
  queue — you can sort a flat list, never a tree walk.

### ✅ Step 22 — Debug draw *(done — [tutorial](documentation/docs/23-debug-draw.html))*
- **Shipped:** an **immediate-mode line overlay** that finally puts the Step 19/20 geometry on screen. A new pure
  [`DebugDraw`](src/renderer/DebugDraw.hpp)/`.cpp` collector turns shapes into a flat **line-list** (`DebugVertex`
  + `line`/`box`/`frustum`/`ray`/`cross`); `frustum` recovers the camera's world-space corners by unprojecting the
  **NDC cube** through `inverse(viewProj)` (reusing the Step 19 `Mat4` inverse). New unlit shaders
  ([`debug_line.vert`](shaders/debug_line.vert)/`.frag`) + a `debugLinePipeline_` (**LINELIST**, depth-test on /
  **write off**) drawn at the end of `recordScene`; the vertices come across the boundary in a new
  [`FrameView`](src/renderer/FrameView.hpp) `debugLines` span and are uploaded into a **transient**, rebuilt-per-
  frame buffer (`uploadDebugLines`). Lines draw into the HDR target so they appear in `KOI_CAPTURE` too.
  [`GpuRenderer`](src/renderer/GpuRenderer.cpp) exposes `lastCameraViewProjection()` so the demo can **freeze**
  and draw the culling frustum. Demo keys **`G`** (bounds) / **`L`** (light icons) / **`F`** (freeze + show the
  frustum), plus `KOI_DEBUG_DRAW` for headless captures; unit-tested in
  [`tests/test_debug_draw.cpp`](tests/test_debug_draw.cpp). Debug-off renders **byte-identical to Step 21**.
- **Deliberately deferred:** an **x-ray** (depth-test-off) mode; per-vertex **normal** visualization (needs CPU
  vertex data the `Mesh` doesn't retain); a debug **HUD** + **text** rendering (their own later steps); the lines
  are tone-mapped/FXAA'd since they draw into the HDR target.
- **Why here:** it's the visual companion to the just-landed culling/bounds work — you can't debug a wrong AABB or
  a mis-built frustum by reading numbers — and the shared groundwork for the HUD, text, picking, and physics viz.

---

## ⚠️ Architecture pivots — decide early, build late

Most roadmap items are **additive**: skeletal animation or fog can land whenever, and nothing
else changes shape. A few are **pivots** — structural decisions whose retrofit cost grows with
every feature built on the old assumption. None needs to be *built* soon, but every new system
should be designed knowing they're coming:

- ✅ **Render-queue extraction** *(done — Step 20)*. `GpuRenderer` no longer walks the scene graph and
  draws inline; it flattens the tree into a flat `RenderItem` list (*traverse → draw list → submit*,
  [`src/renderer/RenderQueue.hpp`](src/renderer/RenderQueue.hpp)) — the structural first move of the whole
  Scaling track, now made. Already built **on** the list: **transparency sorting** (Step 21). Still to come:
  material/pipeline sorting (a known cost since Step 8), instancing, and deferred shading.
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

**Scaling & performance** — every light is still forward-accumulated; culling now skips off-screen draws.
- ✅ **Render-queue extraction** (traverse → draw list → submit; *the* architecture pivot this whole
  track builds on — see the pivots section) — *done, Step 20* ([`RenderQueue.hpp`](src/renderer/RenderQueue.hpp)).
- ✅ Bounding volumes + **frustum culling** (skip what the camera can't see) — *done, Step 20*: each `Mesh`
  carries an `Aabb`, the camera pass tests it against the `Frustum` (consuming the Step 19 geometry layer).
- **Instanced rendering** (one draw call for many copies).
- Sort the draw list by **pipeline / material** (kill the per-draw rebinding cost known since Step 8).
- **Deferred / clustered / tiled** shading (decouple lighting cost from geometry; scale to many
  lights — the known ceiling of forward shading, flagged in `ARCHITECTURE.md`).
- A **render graph** (declare passes + dependencies; derive order, targets, reuse — see pivots).
- GPU-driven culling via SDL3 GPU **compute** shaders; **occlusion culling** (Hi-Z) as a stretch.
- **SIMD** pass over the hand-rolled [`src/math/`](src/math/) once benchmarks exist to prove it.

**Transparency & blended effects** — the engine was **opaque-only** until Step 21, which added the
first blend-enabled pipeline; smoke, foliage cutouts, and particles are still to come.
- ✅ **Alpha blending** + back-to-front sorting — *done, Step 21* ([tutorial](documentation/docs/22-transparency-and-alpha-blending.html)):
  a second **depth-write-off, blend-on** pipeline, a `Material` **`alphaMode`/`opacity`**, and
  `partitionByBlend` sorting the queue far-to-near (the depth-write trick opaque draws use can't work for
  glass — this is exactly why it needed the render queue to sort).
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
- ✅ **Quaternions** (replace Euler rotations; enable smooth `slerp`) — *done, Step 18*
  ([`src/math/Quat.hpp`](src/math/Quat.hpp)); `Transform` now stores a unit `Quat`.
- ✅ **Geometry utility layer** in [`src/math/`](src/math/): **AABB**, **ray**, **plane**, and
  **frustum** types with intersection tests (plus the `Mat4` inverse) — *done, Step 19*
  ([`src/math/Geometry.hpp`](src/math/Geometry.hpp)). The shared prerequisite of frustum culling, ray-cast
  picking, *and* the physics broadphase — one small, fully unit-testable module unblocks three tracks.
  Its first consumer, **frustum culling**, landed in *Step 20*.
- ✅ **Inverse-transpose normal matrix** (correct normals under non-uniform scale) — *done, Step 19*, wired
  through [`shaders/triangle.vert`](shaders/triangle.vert).

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
- ✅ **Debug draw** *(done — Step 22, [tutorial](documentation/docs/23-debug-draw.html))*: immediate-mode
  **line-list** rendering of bounding boxes, light icons, and the camera **frustum** (unprojected from the NDC
  cube through `inverse(viewProj)`) — a pure [`DebugDraw`](src/renderer/DebugDraw.hpp) collector, a transient
  per-frame vertex buffer, and a depth-tested / write-off overlay pipeline. The visual companion that makes the
  Step 19/20 geometry visible; the groundwork for the HUD, text, picking, and physics viz below. *Still to come:*
  an x-ray (depth-off) mode; drawing surface **normals** per vertex.
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
*Platform & build* track. Its first item — engine/app separation — shipped early (Step 17). The rest is
**deliberately deferred**: the owner has chosen to push the **learning tracks** (rendering, animation,
physics, tooling) further along first, and take up cross-platform CI + the remaining hardening items *after*
them. These stay hard requirements for 1.0 (see *Definition of 1.0*) — just not the near-term priority.

- ✅ **Engine / app separation** *(done — Step 17,
  [tutorial](documentation/docs/18-engine-app-separation.html))* — *the defining milestone for "production
  apps".* The demo moved out of the engine into a [`samples/demo/`](samples/demo) app that consumes
  `koi_core` through the public **`koi::Application`** interface (`onStart`/`onUpdate`/`onEvent`/`frameView`)
  plus a [`FrameView`](src/renderer/FrameView.hpp) render bundle; nothing in `src/` hardcodes demo content
  anymore. Still to do: semantic versioning, deprecation policy, and **API reference docs** (the tutorials
  teach concepts; consumers need reference).
- **Cross-platform CI + golden-image regression** *(deferred — after the learning tracks)* — only
  macOS/Metal has ever executed; the Step 9 `spirv-cross` sampler-swap bug is proof that backend-specific
  breakage is real and invisible to the compiler. A Linux/**Vulkan** CI build (lavapipe, the software driver)
  running the tests plus `KOI_CAPTURE` **golden-image comparisons** turns the existing capture tool into an
  automated net for exactly the "compiles clean, renders wrong" class of bug unit tests can't catch. Validate
  **D3D12**/Windows the same way when hardware allows. (Still required for 1.0 — just not taken up early.)
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
Step 17  engine/app separation                       ✅ done
   │
Step 18  quaternions (Transform rotation, slerp)     ✅ done
   │
Step 19  geometry utils (AABB/ray/frustum) + normal matrix   ✅ done
   │
Step 20  render queue ──▶ frustum culling (consumes Step 19 geometry)   ✅ done
   │
Step 21  transparency + alpha blending (consumes Step 20 queue sort)    ✅ done
   │
Step 22  debug draw (line overlay: AABBs/frustum/light icons — makes Step 19/20 visible)   ✅ done
   │
glTF node hierarchy / Sponza ──▶ alpha-tested cutout        ◀── next (learning thread leads)
   │
sort queue by material / instancing ──▶ cascaded shadows
   │
skeletal animation ──▶ HUD / text (build on debug draw) ──▶ particles
   │
deferred / clustered ──▶ fixed timestep ──▶ physics + audio ──▶ editor + gameplay
   │
CI + golden images ──▶ offline asset pipeline ──▶ packaging   ◀── production hardening, deferred to here
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

- [x] **Engine and app are separate** *(Step 17)*: the demo builds against the public
      `koi::Application` API, and nothing in `src/` hardcodes demo content. (A dedicated API
      *reference* doc — beyond the concept tutorial + header comments — is still to come.)
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
- **Verify visually without a window.** `KOI_CAPTURE=<path.bmp> ./build/koi-demo` renders one frame
  to a BMP (convert with `sips -s format png out.bmp --out out.png`) — the quickest way to confirm
  output. Once golden-image CI exists (Path to 1.0 track), a step that changes rendering also adds
  or refreshes its golden capture.
- **Keep the docs in sync.** Update `ARCHITECTURE.md` on structural changes and `README.md`'s status
  line; append the prompt to `PROMPT.md`; write a `memory/YYYY-MM-DD.md` summary; and generate a
  commit message (the project does not auto-commit).
