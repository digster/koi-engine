# Architecture

This document captures the **big-picture design** of Koi Engine — the parts you
can't see by reading a single file — and the reasoning behind the structural
choices. It evolves as the engine grows; right now it describes the foundation
through Step 24 (window & render loop, the first triangle, geometry in GPU
vertex/index buffers, a 3D cube with hand-rolled math + MVP uniform + depth
buffer, a fly camera driven by keyboard/mouse input with delta-time, a **mesh**
abstraction drawn through a **scene graph** of parent/child transforms,
**textures** sampled onto those surfaces, lighting that shades them, per-object
**materials**, **model loading** from OBJ/glTF files, **shadow mapping**, a
**post-processing** chain — an off-screen HDR target run through bloom,
tone-mapping, and FXAA before it reaches the screen — **multiple lights** of
different kinds (directional/point/spot) accumulated in the shader, physically-based
**PBR** shading via a Cook-Torrance metallic-roughness BRDF, and per-pixel material
**texture maps** — metallic-roughness / AO / **normal maps** applied through a
per-vertex tangent + **TBN** basis, with **mipmaps** and **anisotropic filtering**, a
**skybox** — a **cubemap** environment sampled by direction and drawn behind the scene — and
**image-based lighting**, which bakes that cubemap into irradiance / prefiltered / BRDF-LUT maps so
the environment lights the surfaces themselves and metals reflect their surroundings, and **glTF PBR
material import** — reading a model's base-colour / metallic-roughness / normal / AO / **emissive** maps
straight from the file (PNG/JPG decoded by **stb_image**, colour textures handled as **sRGB**), proven by
loading the Khronos **Damaged Helmet**, an **engine/app separation** that lifts the demo out of
the engine into a `samples/` app behind a public `koi::Application` interface, **quaternion** rotations that
replace Euler angles in `Transform` (no gimbal lock, `slerp`-able), a **geometry-utility layer** — AABB /
ray / plane / frustum plus the `Mat4` **inverse** — whose first payoff is an inverse-transpose **normal
matrix** for correct lighting under non-uniform scale, and a **render-queue** extraction that flattens the
scene-graph walk into a flat draw list — the architecture pivot behind **frustum culling** (skip off-screen
draws), **transparency** — a second blend-on/depth-write-off pipeline with a back-to-front sort for see-through
surfaces — and **debug draw**, an immediate-mode **line** overlay (bounding boxes, light icons, the camera
frustum) that makes the otherwise-invisible geometry layer visible on screen, a **HUD / text** overlay —
an embedded bitmap-font atlas drawn as crisp screen-space quads in a final pass after post-processing — and
**instanced rendering**, which sorts that draw list by batch key and collapses identical objects into single
instanced draw calls across the colour and shadow passes).

## Guiding principles

1. **Learn by building.** Every subsystem is introduced when a milestone needs
   it, never speculatively. Code is heavily commented; the
   [`docs/tuts/`](docs/tuts/index.html) tutorials explain the underlying graphics
   *concepts* for newcomers.
2. **Modern, explicit graphics.** We use the SDL3 **GPU API** (Metal/Vulkan/D3D12
   behind one interface) rather than the deprecated OpenGL, so the concepts we
   learn are the ones in use today.
3. **RAII everywhere.** Resources (window, GPU device) are owned by objects whose
   destructors release them, in reverse order of construction. No manual cleanup
   bookkeeping.

## Subsystem map

```
   samples/demo/main.cpp
         │ creates + runs
         ▼
   ┌──────────────┐   drives (onStart/onUpdate/    ┌───────────────────────┐
   │   Engine     │ ──── onEvent/frameView) ─────►  │  Application (DemoApp) │
   │ lifecycle +  │ ◄─── services (renderer(),      │  owns scene / camera / │
   │  main loop   │        requestQuit()) ────────  │  lights / post; hooks  │
   └──────┬───────┘        + FrameView (what to draw)└───────────────────────┘
     creates ┌───┴────┐ creates
             ▼        ▼
     ┌────────────┐  ┌──────────────┐
     │  Window    │  │ GpuRenderer  │
     │ (SDL_Window│  │ (SDL_GPUDevice
     │  RAII)     │  │  + swapchain)│
     └────────────┘  └──────────────┘
             ▲              │
             └── claims ────┘  (renderer attaches its
                                swapchain to the window)

   Log.hpp  ── cross-cutting: leveled logging used by everyone
```

Step 17 drew the **engine/app boundary**: `koi_core` (everything under `src/`) is the reusable
engine and knows nothing about any particular scene; the demo lives under `samples/demo/` as a
`koi::Application` that the engine *drives* by inversion of control. The two talk through a tiny
interface ([`Application`](src/core/Application.hpp)) plus a per-frame [`FrameView`](src/renderer/FrameView.hpp)
bundle. See the *Engine / app separation (Step 17)* section below.

| Component | File | Responsibility |
|-----------|------|----------------|
| `Engine` | [src/core/Engine.*](src/core/) | Owns the **reusable machinery only**: SDL, the `Window`, the `GpuRenderer`, and the main loop (delta-time, event pump, OS-quit, the headless `KOI_CAPTURE`/`KOI_MAX_FRAMES` paths), plus startup/shutdown order. Owns **no** scene/camera/lights. `run(Application&)` drives an app by inversion of control (`onStart`/`onUpdate`/`onEvent`/`frameView`/`onShutdown`) and exposes services to it (`renderer()`, `requestQuit()`). Step 17. |
| `Application` | [src/core/Application.hpp](src/core/Application.hpp) | Header-only abstract interface (Step 17) — the engine/app boundary. Hooks: `onStart` (build content), `onUpdate(dt)` (animate + input), `onEvent` (per-event input; default no-op), `frameView()` (return a `FrameView` to draw), `onShutdown` (release GPU resources while the device is still alive). An app subclasses it and holds its own scene/camera/lights. |
| `FrameView` | [src/renderer/FrameView.hpp](src/renderer/FrameView.hpp) | Header-only value (Step 17): the per-frame render bundle — clear colour, `view` matrix, scene `root` (non-owning), `cameraPos`, `lights` span, `PostSettings`, (Step 22) a non-owning `debugLines` span of overlay geometry, and (Step 23) a non-owning `hudVertices` span of screen-space HUD geometry. Produced by `Application::frameView()`, consumed by both `renderFrame` and `captureFrame`, so the live and headless paths can't drift. Owns nothing. |
| `Window` | [src/core/Window.*](src/core/) | RAII wrapper around one `SDL_Window`. Owns the OS window; the renderer attaches a swapchain to it. |
| `GpuRenderer` | [src/renderer/GpuRenderer.*](src/renderer/) | Owns the `SDL_GPUDevice`, the main pipeline, the depth texture, a shared **sampler** (Step 13: mipmap + **anisotropic** filtering), two neutral 1×1 **fallback maps** (`whiteTex_`, `flatNormalTex_`), the (Step 9) **shadow map** + sampler + depth-only **shadow pipeline**, the (Step 10) **post-processing** resources (off-screen HDR scene target, half-res bloom ping-pong pair, LDR target, a clamp sampler, and four fullscreen pipelines), the (Step 14) **skybox** resources (a `CUBE` **cubemap** texture, a CLAMP_TO_EDGE cubemap sampler, a skybox pipeline, and the cube mesh drawn around the camera), and the (Step 15) **image-based-lighting** resources (three bake pipelines, a trilinear CLAMP sampler, and the baked **irradiance** cube + **prefiltered** cube + **BRDF LUT** targets). Is a **factory** for meshes (`createMesh`, 16- or 32-bit indices) and textures (`loadTexture`, now generating **mipmaps** at upload and choosing an **sRGB** format for colour maps — Step 16; `createTextureFromRGBA` for already-decoded bytes; `loadCubemap` uploads six faces into one cube texture, then `bakeIbl` bakes the IBL maps from it) and a **consumer** of a scene. `renderSceneAndPost` (shared by the live and capture paths) **flattens the scene graph into a render queue once** (`buildRenderQueue`, Step 20), then runs: **shadow pass** (loops the *whole* queue, depth only) → **scene pass** into the HDR target (`recordScene`; **frustum-culls** the queue to the camera then submits each survivor via `submitItem` — binding the material's four maps per draw + the **emissive** map at slot 8 (Step 16), shadow map at slot 4, the three IBL maps at slots 5–7 — drawing in three ordered sub-passes (Step 21): **opaque** items (depth-write on) → the **sky** via `recordSkybox` → **transparent** items through a second **blend pipeline** (depth-write off), sorted back-to-front by `partitionByBlend`; `bindFrameLighting` re-binds the shadow/IBL maps after the pipeline switch) → **post chain** (`runPostChain`: bloom → tone-map composite → FXAA into the final image). Owns no geometry/texture data; holds a reused `renderQueue_` (+ `opaqueItems_`/`transparentItems_` scratch), both the opaque `trianglePipeline_` + the `transparentPipeline_`, and `frustumCullingEnabled_` / `transparentSortEnabled_` toggles. |
| `PostProcess` | [src/renderer/PostProcess.hpp](src/renderer/PostProcess.hpp) | Header-only, SDL-free: the `PostSettings` knobs (exposure, bloom threshold/intensity, per-effect toggles) the **app** drives from input and hands to the renderer (via `FrameView`), plus pure helpers (`halfExtent`, `luminance`, `acesToneMap`) that mirror the shader math for headless unit tests (`tests/test_post.cpp`). |
| `Material` | [src/scene/Material.hpp](src/scene/Material.hpp) | A header-only appearance bundle: an `albedo` texture + **PBR** params `metallic`/`roughness` (Step 12) — now **factors** — plus (Step 13) three optional map handles: `metalRough` (glTF-packed G=roughness, B=metallic), `normalMap` (tangent-space), and `ao`; and (Step 16) an optional `emissive` map + `emissiveFactor` (default 0 ⇒ no glow, so existing materials are unchanged). Any map may be null; the renderer binds a neutral 1×1 fallback so map-less materials render as in Step 12. Since **Step 21** it also carries an **`alphaMode`** (`Opaque`/`Blend`) + **`opacity`** (glTF baseColorFactor.a) that route the material to the opaque or the blended transparent pass. Shared by `shared_ptr`; a `Node` references one; the renderer binds all five maps per draw. |
| `Pbr` | [src/renderer/Pbr.hpp](src/renderer/Pbr.hpp) | Header-only, SDL-free (Step 12): pure CPU mirrors of the Cook-Torrance BRDF sub-terms — `distributionGGX` (D), `geometrySmith`/`geometrySchlickGGX` (G), `fresnelSchlick` (F), plus `kPi`. The shader in `triangle.frag` is the runtime truth; these exist so the microfacet math's shape is unit-tested headlessly (`tests/test_pbr.cpp`), the same pattern as `PostProcess.hpp`/`Light.hpp`. |
| `Light` | [src/scene/Light.hpp](src/scene/Light.hpp) | Header-only, SDL-free (Step 11): a `Light` struct (type = directional/point/spot, position, direction, colour, intensity, range, spot-cone cosines, an `enabled` flag) plus pure helpers (`attenuation`, `spotFactor`, `activeLightCount`) that mirror the shader math for headless tests (`tests/test_light.cpp`). The **app** (`DemoApp`) owns a `std::vector<Light>` and hands it to the renderer each frame via `FrameView`, exactly like the camera + `PostSettings`. `MAX_LIGHTS` matches the shader's fixed array size. |
| `ModelLoader` | [src/renderer/ModelLoader.*](src/renderer/) | `loadModel(path)` reads a model file into a **`LoadedModel`** (a `Mesh` **+** an optional `Material`): `.obj` via **tinyobjloader** (de-duplicating its separate v/vt/vn indices; geometry only — material null), `.glb/.gltf` via **cgltf**, which since **Step 16** also imports the file's PBR **material** (base-colour / metallic-roughness / normal / occlusion / **emissive** maps + factors). Its images (PNG/JPG) are decoded by **stb_image** — embedded `.glb` `buffer_view`s straight from memory, external URIs via `loadTexture` — with colour maps flagged **sRGB**. The single TU that pulls in those three third-party headers (defines their `*_IMPLEMENTATION`) — compiled warning-free (`-w`). |
| `Mesh` | [src/renderer/Mesh.*](src/renderer/) | RAII owner of one shape's vertex + index buffers (+ index count + element size: 16-bit for primitives, 32-bit for big loaded models). Also caches a **model-space `Aabb`** (Step 20), computed from the vertices in `createMesh` — the box frustum culling transforms + tests. Holds a **non-owning** device pointer used only to release those buffers — so every `Mesh` must die before the renderer's device. Created via `GpuRenderer::createMesh`; held by `shared_ptr` so many nodes share one. Non-copyable/non-movable. |
| `RenderQueue` | [src/renderer/RenderQueue.*](src/renderer/) | The Step 20 flat draw list. `RenderItem { mesh, material, world, worldBounds }`; `computeLocalBounds` (a mesh's model-space box), `buildRenderQueue` (walk the node tree once → flat list), `cullToFrustum` (keep items whose world bounds hit the camera `Frustum`), and (Step 21) `partitionByBlend` (split the culled list by `Material::alphaMode` into opaque + a **back-to-front-sorted** transparent bucket), plus (Step 24) `DrawBatch` + `sortByMeshMaterial` (one stable pointer-key sort — mesh primary, material secondary — shared by both passes) + `coalesceBatches` (runs of equal key → one instanced draw). The pure helpers are unit-tested with no GPU (`tests/test_render_queue.cpp`); the *traverse → list → submit* split is the pivot that culling/sorting/**instancing**/transparency all build on. |
| `DebugDraw` | [src/renderer/DebugDraw.*](src/renderer/) | The Step 22 immediate-mode line overlay. A pure, GPU-free collector: `DebugVertex { position, color }` plus `line`/`box`/`frustum`/`ray`/`cross` building a flat **line-list** vertex array (`box` and `frustum` share a 12-edge topology; `frustum` unprojects the NDC cube through `inverse(viewProj)`). Rebuilt every frame by the app and handed over via `FrameView::debugLines`; the renderer uploads it into a **transient** vertex buffer (recreated per frame) and draws it through a minimal unlit `debugLinePipeline_` (LINELIST, depth-test on / **write off**) in `recordDebug` at the end of `recordScene`. Unit-tested with no GPU (`tests/test_debug_draw.cpp`). |
| `Hud` / `Font` | [src/renderer/Hud.*](src/renderer/), [src/renderer/Font.hpp](src/renderer/Font.hpp) | The Step 23 immediate-mode **HUD / text** overlay. `Font.hpp` is the embedded public-domain **8×8 bitmap font** table + atlas layout (`glyphCell`/`cellUV` with a half-texel inset), the single source of truth shared by the collector and the atlas bake. `Hud` is a pure, GPU-free collector: `HudVertex { pos, uv, color }` plus `text` (one 6-vertex quad per glyph, `\n`-aware, `?` fallback) and `rect` (a panel sampling the reserved solid-white cell) building a flat **triangle-list** in **pixel** coordinates. Rebuilt every frame by the app and handed over via `FrameView::hudVertices`; the renderer bakes the atlas once (`createHudResources`, nearest sampler), uploads the vertices into a **transient** buffer (`uploadHud`), and draws them through a textured, alpha-blended, depth-less `hudPipeline_` (`hud.vert`/`hud.frag`) in a **final overlay pass on the LDR image, after post** (`recordHud`) — so text stays crisp. Unit-tested with no GPU (`tests/test_hud.cpp`). |
| `Texture` | [src/renderer/Texture.*](src/renderer/) | RAII owner of one `SDL_GPUTexture` (sampled image). Same non-owning-device lifetime rule as `Mesh`. Created via `GpuRenderer::loadTexture` (BMP → SDL; PNG/JPG → **stb_image**; decode → upload) or `createTextureFromRGBA` (already-decoded bytes, e.g. embedded glTF images), with an **sRGB** option for colour maps (Step 16); held by `shared_ptr`. A *sampler* (how to read it) is separate, reusable device state owned by the renderer. |
| `Primitives` | [src/renderer/Primitives.*](src/renderer/) | Free functions (`makeCubeMesh`, `makePlaneMesh`) that define a shape's vertex/index data and upload it via `createMesh`, keeping the renderer geometry-agnostic. |
| `Transform` | [src/scene/Transform.hpp](src/scene/Transform.hpp) | Header-only, SDL-free: position / rotation (a unit **`Quat`** since Step 18 — no gimbal lock, `slerp`-able) / scale, plus `localMatrix()` returning `T·R·S` (scale → rotate → translate). Unit-tested in `tests/test_transform.cpp`. |
| `Node` | [src/scene/Node.*](src/scene/) | One element of the scene graph: a local `Transform`, an optional shared `Mesh` (shape) + `Material` (appearance), owned children (`unique_ptr`), and a cached world matrix. Draws only with both a mesh and a material. `updateWorldTransforms` walks the tree computing `world = parentWorld · local`. Unit-tested in `tests/test_node.cpp`. |
| `Camera` | [src/scene/Camera.*](src/scene/) | A yaw/pitch fly camera (position + angles). Produces a `view` matrix via `lookAt`; `processKeyboard`/`addMouseLook` update it from input. Lives above the renderer and knows nothing about the GPU; its header is SDL-free. Logic unit-tested in `tests/test_camera.cpp`. |
| `Vertex` | [src/renderer/Vertex.hpp](src/renderer/Vertex.hpp) | The CPU-side layout of one vertex (3D position + color + uv + normal + **tangent**, 56 bytes since Step 13). Its byte layout is the contract the pipeline's vertex attributes describe; pinned by `static_assert`s and `tests/test_vertex.cpp`. |
| `Tangents` | [src/renderer/Tangents.hpp](src/renderer/Tangents.hpp) | Header-only, SDL-free (Step 13): pure math to derive a per-vertex **tangent** for normal mapping — `triangleTangent` (Lengyel, from positions + UVs) and `orthonormalizeTangent` (Gram-Schmidt vs. the normal, with a safe fallback). Used by `ModelLoader`; unit-tested headlessly (`tests/test_tangent.cpp`), the same CPU-twin pattern as `Pbr.hpp`/`Light.hpp`. |
| `math` (`Vec`, `Mat4`, `Quat`, `Geometry`) | [src/math/](src/math/) | Hand-rolled, header-only vector (`Vec2/3/4`), column-major 4×4 matrix math (translate/rotate/scale/`perspective`/**`orthographic`**/`lookAt`, and Step 19's **`inverse`** + **`transpose`**), a unit **`Quat`** quaternion (Step 18: axis-angle/Euler construction, Hamilton product, sandwich `rotate`, `toMat4`, **`slerp`**), and the Step 19 **geometry layer** [`Geometry.hpp`](src/math/Geometry.hpp) — **`Ray`**, **`Aabb`**, **`Plane`**, **`Frustum`** (z∈[0,1] plane extraction) with ray-box + frustum-box tests. No GLM — written ourselves so nothing stays a black box. Pure, so fully unit-tested in `tests/test_math.cpp` + `tests/test_quat.cpp` + `tests/test_geometry.cpp`. |
| `Shader` | [src/renderer/Shader.*](src/renderer/) | Loads a compiled shader for the active backend: `selectShaderVariant` (pure, unit-tested) picks the format/extension/entry point; `loadShader` reads the file and creates the `SDL_GPUShader` (with the right uniform-buffer and sampler counts). |
| `Log` | [src/core/Log.hpp](src/core/Log.hpp) | Leveled logging macros over SDL's logger; one place to control verbosity. |

## Data flow: one frame

The render loop lives in `Engine::run(app)`; each iteration the engine calls back into the app
(Step 17 — the engine owns the loop, the app owns the content):

```
Engine: processEvents(app) → SDL_PollEvent: OS-quit → stop; every event → app.onEvent(engine, ev)
       │                       (DemoApp.onEvent: Esc → engine.requestQuit(); keys 1–9 toggles;
       │                        mouse motion → camera.addMouseLook)
dt               → dt = ΔSDL_GetTicksNS (clamped)
       │
app.onUpdate(engine, dt) → camera.processKeyboard(polled keys, dt); spin a couple of nodes
                   (rotation = Quat::fromAxisAngle(Y, angle += rate·dt)); sceneRoot.updateWorldTransforms() caches world matrices
       │
fv = app.frameView()   → bundle {clearColor, view = camera.viewMatrix(), root = sceneRoot,
       │                          cameraPos, lights, post}
       │
renderFrame(fv)        → in GpuRenderer; renderSceneAndPost:
                     1. acquire command buffer + swapchain image (+ its size)
                     2. ensureSceneTargets: depth + HDR scene + half-res bloom + LDR match size
                     3. SHADOW PASS: compute lightViewProj from the SUN's direction
                        (lights[0]); render scene depth into the shadow map (depth-only)
                     4. SCENE PASS: begin render pass into the HDR target (color + depth CLEAR);
                        recordScene — bind pipeline + shadow map (slot 4) + the 3 IBL maps
                        (slots 5–7, Step 15), pack the light LIST + lightViewProj into the light
                        UBO once (Step 11: an array of directional/point/spot lights, only the
                        sun shadowed; ambient.w carries the IBL on/off flag), then walk
                        the tree (per node: bind the material's 4 maps [albedo/metal-rough/
                        normal/AO, slots 0–3] + push material params, bind mesh buffers +
                        push {mvp,model}, draw); then recordSkybox draws the SKY LAST (Step 14:
                        cube around the camera, translation-stripped view, far-plane depth,
                        LEQUAL) so it fills only background pixels; end pass
                     5. POST CHAIN (runPostChain): bloom bright-pass + separable blur → composite
                        (exposure, ACES tone-map, vignette, gamma) → FXAA into the swapchain image
                     6. submit command buffer (queues present)
```

See [docs/tuts/01-window-and-render-loop.html](docs/tuts/01-window-and-render-loop.html),
[docs/tuts/02-first-triangle.html](docs/tuts/02-first-triangle.html),
[docs/tuts/03-vertex-and-index-buffers.html](docs/tuts/03-vertex-and-index-buffers.html),
[docs/tuts/04-3d-cube-mvp-and-depth.html](docs/tuts/04-3d-cube-mvp-and-depth.html),
[docs/tuts/05-camera-and-input.html](docs/tuts/05-camera-and-input.html), and
[docs/tuts/06-meshes-and-scene-graph.html](docs/tuts/06-meshes-and-scene-graph.html) for the
concept-by-concept explanation.

### Shaders & the build-time toolchain

GPUs run compiled bytecode, and each backend wants a different dialect (SPIR-V for
Vulkan, MSL for Metal). We author shaders once in **GLSL** (`shaders/*.vert`,
`*.frag`) and the build compiles them in two stages:

```
shaders/triangle.vert ──glslc──► build/shaders/triangle.vert.spv ──spirv-cross──► …vert.msl
                                          (Vulkan)                       (Metal)
```

CMake's `koi_add_shader()` registers these commands; the `koi_shaders` target
builds the outputs into `build/shaders/`, beside the executable. At runtime
`loadShader` (via `selectShaderVariant`) loads whichever variant the active device
supports — `.msl` (entry `main0`) on Metal here. The pipeline is built once at
startup and bound each frame.

### Geometry: vertex & index buffers

Geometry lives in GPU memory, not in the shader (Step 1's approach) or the C++
heap. Shaders can only read GPU resources, and the fast memory a vertex buffer
wants is not CPU-writable, so getting data onto the GPU is a **two-step move**:

```
Vertex[] in CPU RAM ──memcpy──► transfer (staging) buffer ──copy pass──► vertex buffer (GPU)
```

`GpuRenderer::uploadToGpuBuffer()` encapsulates that move (create GPU buffer →
create + map an UPLOAD transfer buffer → memcpy → copy pass → submit) and is reused
for both the vertex buffer and the index buffer. As of Step 5 the public entry point
is `createMesh(vertices, indices)`, which runs that upload twice and wraps the two
buffers in a `Mesh`. This is the mirror image of `captureFrame`, which uses a
*download* transfer buffer to read pixels back. Uploads run when a mesh is created;
no fence is needed because command buffers execute in submission order, so the later
per-frame draw sees the finished copy.

The pipeline carries a **vertex input layout** — a buffer description (slot, pitch =
`sizeof(Vertex)`, per-vertex rate) plus one attribute per shader input (location,
format, byte offset). The attribute `location`s must match `triangle.vert`'s
`layout(location=N) in` declarations and `koi::Vertex`'s field offsets; that chain
(GLSL → SPIR-V → MSL `[[attribute(N)]]` → SDL attribute) is the contract the
`Vertex` `static_assert`s guard. The **index buffer** lets the cube's 12 triangles
reuse its 8 corner vertices (36 indices → 8 vertices) via `SDL_DrawGPUIndexedPrimitives`.

### Math, the MVP transform & the depth buffer (Step 3)

The cube lives in 3D **model space** and is carried to clip space by a
**Model-View-Projection** matrix. The math is hand-rolled (no GLM) in the
header-only `src/math/` module: `Mat4` is **column-major** (matching GLSL's
`mat4` memory layout, so it copies into the uniform with no transpose) and
`perspective()` produces depth in the **0…1** range SDL3's GPU API expects
(Metal/Vulkan/D3D), not OpenGL's −1…1. The View matrix is identity for now (a
real camera is Step 4); the camera distance is folded into the model translate.

The MVP is a **per-draw constant**, so it travels via a **uniform buffer** rather
than per-vertex attributes: `GpuRenderer::buildMvp()` builds it each frame (a
time-based spin) and `SDL_PushGPUVertexUniformData(cmd, 0, …)` hands it to the
vertex shader. SDL's SPIR-V convention puts vertex-stage uniform buffers in
descriptor set 1 (`layout(set = 1, binding = 0)`), and `loadShader` is told the
shader uses one uniform buffer.

A **depth buffer** makes the solid cube correct: a depth texture (sized to match
the swapchain, recreated on resize by `ensureDepthTexture`) is cleared to 1.0 each
frame and attached to the render pass; the pipeline's depth test (`compare_op =
LESS`, depth write on) keeps only the nearest fragment per pixel, so face draw
order no longer matters. The projection's aspect term also fixes Step 2's stretch.

### Camera, input & delta-time (Step 4)

The **View** in MVP (identity in Step 3) is now produced by a `Camera` in
`src/scene/`. It's a yaw/pitch fly camera: from two angles it derives a `forward`
vector and builds a view matrix with `Mat4::lookAt` — which transforms the world by
the camera's inverse (moving the camera is the same as moving the world the other
way). The camera sits *above* the renderer: `Engine` owns it, updates it from input,
and passes its `viewMatrix()` into `renderFrame`/`captureFrame`; the renderer stays
ignorant of cameras and input.

Input is split by nature. **Continuous** movement reads the live key array from
`SDL_GetKeyboardState` each frame (so held keys move smoothly), scaled by
**delta-time** (`SDL_GetTicksNS` deltas, clamped) so speed is frame-rate independent.
**Discrete** input stays event-driven in `processEvents`: quit/Escape, and mouse
motion under SDL **relative mouse mode** (cursor hidden + locked, motion as deltas)
feeding `addMouseLook`. Relative mode is enabled only for interactive runs, not the
headless `KOI_MAX_FRAMES`/`KOI_CAPTURE` paths.

### Meshes & the scene graph (Step 5)

Step 5 removes the last bit of hardcoding from the renderer. Geometry moved out of
`GpuRenderer` into reusable **`Mesh`** objects, and the flat cube list became a
**scene graph**: a tree of **`Node`**s, each with a local **`Transform`** (TRS) and
an optional shared `Mesh`.

```
            Mesh (renderer/)             scene graph (scene/)
   vertices+indices in GPU buffers   Node ── Transform (T·R·S) ── localMatrix()
   shared_ptr, made by createMesh     │└── shared_ptr<Mesh>   (may be null = group)
                                       └── unique_ptr children…  → a tree
```

Two passes per frame keep concerns separate: **(1)** `Node::updateWorldTransforms`
walks the tree top-down computing each node's world matrix as
`world = parentWorld · local` (so a parent's motion propagates to all descendants —
the whole point), then **(2)** the renderer draws each node at `proj·view·world`.
*(As of Step 20 that second pass no longer recurses the tree inline: `buildRenderQueue`
flattens it into a **render queue** first, which the passes then iterate — see the
Step 20 section. Same result, but now filterable/sortable.)* Meshes are
`shared_ptr` (many nodes reuse one cube); children are `unique_ptr` (a node owns its
subtree). Layering note: `Node` (in `scene/`) references `Mesh` (in `renderer/`) — a
deliberate dependency, since a node that draws must name *what* geometry to draw;
`Camera` stays GPU-free. `Mat4` gained `scaling()` to support `Transform`.

> **Lifetime rule (RAII):** a `Mesh` frees its GPU buffers through the renderer's
> device, which it does *not* own. So every mesh must die before the device. The
> `Engine` enforces this by releasing the scene **before** the renderer in
> `shutdown()` (and by declaring `sceneRoot_` after `renderer_`, so default member
> destruction order agrees). Splitting `renderFrame` into `begin/draw/end` was
> considered and deliberately deferred — not needed yet.

### Textures & samplers (Step 6)

Surfaces are now painted by an **image** instead of only vertex colors. `Vertex`
gained a 2D **uv** (texture coordinate), so the cube split from 8 shared corners to
**24 vertices** — a per-face attribute (uv now, normals later) can't live on a shared
corner. The fragment shader samples a texture and multiplies by the vertex color
(now a tint): `outColor = texture(uTex, vUV) * vColor`.

Getting the image onto the GPU reuses the **staging move** from Step 2, in 2D:
`SDL_LoadBMP` → `SDL_ConvertSurface` (to RGBA32, matching `R8G8B8A8_UNORM`) →
`uploadToGpuTexture` (the sibling of `uploadToGpuBuffer`: map a transfer buffer,
copy, `SDL_UploadToGPUTexture`). It's the mirror of `captureFrame`'s pixel download.

```
   Texture (renderer/)            sampler (renderer-owned)        binding
   SDL_GPUTexture, RAII           LINEAR filter, REPEAT wrap      set=2, slot 0
   made by loadTexture            (how to read any texture)       BindGPUFragmentSamplers
```

A **sampler** (filtering between texels + wrap mode past the edges) is separate,
reusable device state, so the renderer owns one shared sampler and pairs it with
whatever `Texture` it draws. SDL3's resource sets are fixed: vertex uniform buffers
at `set=1` (the MVP), **fragment sampled textures at `set=2`**; `loadShader` now
declares the fragment shader's sampler count, and `recordScene` binds the
texture+sampler once before the traversal (one texture for the whole scene this
step — per-mesh materials are later). The image asset (`assets/uv_grid.bmp`) is
copied next to the binary by CMake's `koi_assets` target and loaded at runtime via
`SDL_GetBasePath()`, mirroring the shader pipeline. Same lifetime rule as meshes: the
`Texture` is released before the renderer's device in `shutdown()`.

### Lighting & normals (Step 7)

Surfaces now respond to light. `Vertex` gained a **normal** (the direction it faces);
the cube's 24-vertex split from Step 6 already lets each face carry its own flat
normal. Lighting is computed **in world space**, which forces one structural change:
the vertex shader needs the **model matrix on its own** (not just the combined MVP) to
output each fragment's world position + world normal. So the vertex uniform grew to
`{ mvp, model }`, pushed per node (`model` is the node's `worldMatrix()`). World normal
= `mat3(model) * normal` — correct for the scene's **uniform** scale. *(Step 19 lifted this
restriction: the uniform is now `{ mvp, model, normalMatrix }` with `normalMatrix =
transpose(inverse(model))`, so normals stay correct under non-uniform scale — the tangent
keeps riding `model`. See the geometry-utilities step.)*

```
   vertex shader (set=1)            fragment shader
   uniform { mvp, model }   ──►     ambient + diffuse(N·L) + specular(N·H)  over
   outputs world pos+normal         albedo = texture(set=2) · vColor
                                    light params: uniform LightUBO (set=3)
```

The fragment shader implements **Blinn-Phong**: ambient (constant fill) + diffuse
(`max(dot(N,L),0)`, a directional "sun") + specular (halfway vector `H=normalize(L+V)`,
needing the camera position), all modulating the textured albedo. The light is
frame-constant, so its parameters live in a **fragment uniform buffer at `set=3`**
(`loadShader` now declares the fragment shader's uniform-buffer count too), pushed once
per frame in `recordScene` via `SDL_PushGPUFragmentUniformData`. The camera position is
threaded through `renderFrame`/`captureFrame` (from `Camera::position()`) for the
specular term. Light parameters are constants for now — a per-object **material** is the
natural next refinement.

### Materials (Step 8)

Step 8 removes the "one global texture / hardcoded shininess" limitation so objects
can look different. A **`Material`** (in `scene/`) bundles a surface's appearance — a
`shared_ptr<Texture>` plus specular params — and a `Node` references one alongside its
mesh. This completes a clean three-way split: a drawn object is a **shape** (`Mesh`),
a **placement** (`Transform`/`Node`), and an **appearance** (`Material`), each varying
independently (the demo's hub and satellites share a texture but differ in shininess).

```
   Node ─┬─ Mesh        (shape)
         ├─ Transform   (placement)
         └─ Material ─→ Texture + shininess/specStrength   (appearance)
```

The binding model shifts accordingly: the texture, which Step 7 bound **once per
frame**, now binds **per draw** inside `recordNode` (different nodes use different
textures) — the same per-node rhythm already used for mesh buffers. The fragment
shader gains a **second** uniform buffer: light at `set=3,binding=0` (frame-constant,
pushed once in `recordScene`) and **material at `set=3,binding=1`** (per-draw, pushed
in `recordNode`); `loadShader` declares two fragment uniform buffers. The single
global `Engine::texture_` is gone — textures now live in materials owned by the scene,
so the existing "reset `sceneRoot_` before `renderer_`" teardown frees them in order.
The cost (rebinding per draw even when many objects share a material) is real;
batching/sorting by material is a deferred optimization.

### Models & shadows (Step 9)

Two features land together.

**Model loading.** `ModelLoader::loadModel` reads OBJ (**tinyobjloader**) or glTF
(**cgltf**) into a `Mesh`. These are single-header libraries downloaded at configure
time like `doctest`; the one TU that `#define`s their `*_IMPLEMENTATION` is compiled
with warnings off. OBJ indexes position/uv/normal separately, so the loader
de-duplicates each unique triple into one combined `Vertex`; glTF stores combined
vertices already. Real models can exceed 65 535 vertices, so `Mesh` now carries a
16- or 32-bit index element size and `createMesh` has a `Uint32` overload. *(As of Step 16
`loadModel` returns a `LoadedModel` — mesh **+** the glTF file's imported material; OBJ
still yields geometry only. See* glTF PBR material import (Step 16) *below.)*

**Shadow mapping** is a two-pass technique:

```
   PASS 1  renderShadowPass:  scene depth → shadow map (depth-only pipeline,
           from the light's orthographic view; lightViewProj·world per node)
   PASS 2  recordScene:       main draw; the fragment shader projects worldPos by
           lightViewProj, samples the shadow map, compares depth (bias + 2x2 PCF)
           → occluded fragments lose diffuse + specular (keep ambient)
```

The renderer owns a sampleable depth **shadow map** (`DEPTH_STENCIL_TARGET | SAMPLER`),
a CLAMP **shadow sampler**, and a depth-only **shadow pipeline** (`shadow.vert` + an
empty `shadow.frag`, 0 color targets, with a depth bias to fight acne). `Mat4` gained
`orthographic` for the directional light's frustum; the light's view-projection is
computed once per frame and used by *both* passes (so they agree). The main fragment
shader gains a second sampler (the shadow map at `set=2,binding=1`) and `lightViewProj`
in the light UBO.

> **Toolchain note:** with two fragment samplers, `spirv-cross` ordered them by usage
> rather than by `layout(binding=)`, swapping them on Metal. Fixed by compiling MSL
> with **`--msl-decoration-binding`**, so resource indices follow the GLSL bindings —
> which is what SDL's slot-based binding expects.

### Post-processing (Step 10)

The scene stops rendering straight to the swapchain. Instead it renders into an engine-owned
**off-screen HDR target** (`R16G16B16A16_FLOAT`), and a chain of **fullscreen passes** then
processes that image before it reaches the screen. The HDR format is the enabling choice:
highlights can exceed 1.0, so tone-mapping has range to compress and bloom has genuine bright
areas to extract — an 8-bit target would clip both away.

```
  scene ─► sceneHDR ─► bloom bright-pass ─► separable blur (½-res, ping-pong)
                              │                        │
                              └──────────► composite ◄─┘  (scene + bloom, exposure,
                                              │            ACES tone-map, vignette, gamma)
                                              ▼
                                             ldr ─► FXAA ─► swapchain
```

Each pass draws a **fullscreen triangle** — three vertices synthesized from `gl_VertexIndex`
in `fullscreen.vert`, no vertex buffer, no depth — the standard buffer-free way to run a shader
over every pixel. Ordering is dictated by colour space: **bloom** reads linear HDR (before
tone-mapping); **composite** applies exposure, the ACES curve, the vignette, and the
**linear→sRGB gamma** encode; **FXAA** runs last on the final gamma-encoded LDR image because
it judges edges by *perceived* luma. The main pipeline's colour format changed from the
swapchain's to the HDR one; the off-screen targets are lazily (re)created to match the window
size by `ensureSceneTargets` (the same pattern the depth buffer already used). The whole
sequence lives in `renderSceneAndPost`, shared by `renderFrame` and `captureFrame` so the
headless BMP always matches the window. `PostSettings` (in `renderer/PostProcess.hpp`) carries
the per-effect toggles + exposure, driven from the keyboard in `Engine::processEvents`.

### Multiple lights (Step 11)

The single hardcoded directional "sun" becomes a **list** of lights of three kinds —
**directional** (parallel rays, no falloff), **point** (a position that fades with
distance), and **spot** (a point light confined to a cone). Light is **additive**, so
the fragment shader just **loops** over the active lights and sums each one's
Blinn-Phong contribution (ambient added once, up front). The vertex shader is
unchanged — it already emits world position + normal, all the loop needs.

```
   scene/Light.hpp (SDL-free)          shader (triangle.frag)
   Light{type,pos,dir,color,           for i in 0..lightCount:
     intensity,range,cone cos,           L,atten by type; Blinn-Phong;
     enabled}                            shadow only if i==0 & directional;
   + attenuation()/spotFactor()          result += radiance * (...)
   + activeLightCount()   ── mirror ──►  (helpers unit-tested headlessly)
```

Two new concepts arrive with point/spot lights. **Attenuation** — a point light's
brightness follows the inverse-square law (`1/(d²+1)`) multiplied by a range window
(`clamp(1−(d/range)⁴,0,1)²`) that forces an exact 0 at `range`, giving each light a
finite radius. **Spot cone** — the beam fades between an inner and outer half-angle;
we compare **cosines** (`dot(L, aim)` vs the cutoffs, via `smoothstep`) to skip an
`acos`, remembering that cosine *decreases* with angle (inner cutoff is the larger
cosine). Both are duplicated as pure CPU helpers in `Light.hpp` and unit-tested in
`tests/test_light.cpp` — the shader stays the runtime source of truth.

The lights travel to the shader as a **fixed-size `MAX_LIGHTS` array** in the light
UBO (`set=3,b=0`) — a uniform buffer's layout is fixed at pipeline-build time, so the
array can't be dynamic. Each `GpuLight` is packed as exactly **four `vec4`s** (64
bytes) so the C++ struct in `recordScene` and the GLSL struct agree byte-for-byte
under **std140** (a stray `vec3` would shift the whole array — the same alignment
class as Step 9's sampler-ordering bug). The `Engine` owns a `std::vector<Light>`
(like the camera + `PostSettings`), threads it through
`renderFrame`/`captureFrame` → `renderSceneAndPost` → `recordScene` as a
`std::span<const Light>`, animates one orbiting point light, and toggles light groups
from the keyboard (`5`/`6`/`7`); disabled lights are simply skipped when packing.

**Shadows stay on the sun only.** Each shadow-caster needs its own map (a point light
needs six — a cube map), so this step keeps the single Step-9 shadow map, driven by
`lights[0]`'s direction; the shader applies the shadow factor only to that light.
The fixed-array loop is also why "hundreds of lights" is a *different* architecture
(deferred / clustered shading) — a deliberate future milestone, not this step.

### PBR materials (Step 12)

The shading *inside* the Step 11 light loop switches from ad-hoc **Blinn-Phong** to a
physically-based **Cook-Torrance metallic-roughness** BRDF. This is a deliberately
contained change: no new vertex attributes, no new textures, no binding changes — the
`Material`'s two Blinn-Phong scalars (shininess/specStrength) simply become
`metallic`/`roughness`, pushed through the *same* material-uniform `vec4` lanes.

```
   scene/Material.hpp            shader (triangle.frag) per light:
   {texture, metallic,           F0   = mix(0.04, albedo, metallic)
    roughness}                   spec = D·G·F / (4·(N·V)(N·L))   (GGX/Smith/Fresnel)
   renderer/Pbr.hpp  ── mirror ─► kd   = (1-F)(1-metallic)        (metals: no diffuse)
   D/G/F helpers (unit-tested)   Lo  += shadow·(kd·albedo/π + spec)·radiance·(N·L)
```

The **specular** term is the microfacet BRDF: `D` (GGX/Trowbridge-Reitz) is how many
facets face the halfway vector — roughness widens this lobe; `G` (Smith with
Schlick-GGX) is microfacet self-shadowing; `F` (Fresnel-Schlick) is the grazing-angle
reflectance rise. The **metallic** parameter selects behaviour via the base
reflectance `F0`: dielectrics reflect a colourless ~4% and keep a diffuse albedo;
metals reflect their *own* albedo and have **no diffuse** (`kd` scaled by
`1-metallic`). **Energy conservation** weights diffuse by `1-F` and divides the
Lambertian term by `π` — which is why `setupLights`' intensities are larger than the
Blinn-Phong era (same brightness, honest math). The three D/G/F terms have pure CPU
twins in `renderer/Pbr.hpp`, unit-tested in `tests/test_pbr.cpp`.

At this step the ambient term is still a crude `ambient·albedo` fill: with only direct lights,
**metals look dark** away from their sharp reflections (a metal has no diffuse and no environment to
reflect). That is physically correct, and it's exactly the gap **image-based lighting (IBL)** closes
in [Step 15](#image-based-lighting-step-15) — the environment finally supplies that ambient.

### Texture & normal maps (Step 13)

Step 12's material parameters were **scalars** — one `metallic`/`roughness` for a whole
object. Step 13 drives them (and the surface normal) **per pixel** from texture maps, and
adds fine relief with **normal maps**. The Cook-Torrance BRDF above is *unchanged*; only
its inputs now come from images.

```
   scene/Material.hpp                shader (triangle.frag) per fragment:
   {albedo, metallic, roughness,     N   = TBN · (normalMap·2−1)         (perturbed normal)
    metalRough, normalMap, ao}       metallic  = factor · mr.b           (glTF: B=metal)
   renderer/Tangents.hpp ─ mirror ─► roughness = factor · mr.g           (glTF: G=rough)
   T = per-vertex tangent            color = ambient·albedo·ao + Lo(N,…) (AO on ambient)
```

Three structural additions:

- **A per-vertex tangent.** A normal map stores directions in **tangent space** (relative
  to the UV layout), so the shader rotates a sampled normal into world space with the
  **TBN** basis `mat3(T, B, N)` — `N` from Step 7, `B = N×T`, and `T` the new attribute.
  `Vertex` grows a fifth `tangent` (pitch 44→56); the pipeline gains a 5th vertex
  attribute. Primitives hardcode the tangent per face; `ModelLoader` computes it (Lengyel,
  in `renderer/Tangents.hpp`) or reads glTF's `TANGENT`. We store a `vec3` tangent and take
  `B = N×T` — correct for consistent UV handedness (mirrored UVs would need a signed
  `vec4`, a noted simplification).
- **Five fragment samplers, with fallbacks.** `set=2` now carries the four per-material maps
  (albedo, metallic-roughness, normal, AO) at slots 0–3, bound per draw in `recordNode`, and
  the shadow map moved to slot 4. A material that omits a map binds a neutral **1×1
  fallback** (white, or a flat normal `(0,0,1)`), so the math reduces *exactly* to Step 12 —
  no shader branch / no permutation (a deliberate dodge of the shader-variant pivot).
- **Mipmaps + anisotropic filtering.** With per-pixel detail dominating, minified textures
  alias; `uploadToGpuTexture` now generates a full **mip chain** (`SDL_GenerateMipmapsForGPUTexture`,
  which is why sampled textures also get `COLOR_TARGET` usage), and the shared sampler enables
  **anisotropy** so oblique surfaces (the receding floor) stay sharp instead of over-blurring.

The demo maps come from `tools/gen_textures.py` (a `uv`-run script — a seed of the roadmap's
offline asset-pipeline track) as BMPs. glTF material/texture import + PNG loading (and thus the
**Damaged Helmet**) landed in **Step 16**: `loadModel` now returns a `LoadedModel` (mesh + material),
`loadGltf` imports the file's PBR material, and `loadTexture` decodes PNG/JPG via **stb_image** with
an **sRGB** option for colour maps. See *glTF PBR material import (Step 16)* below.

### Skybox & cubemaps (Step 14)

The scene finally gets a **world behind it**: a **cubemap** sky drawn in the same scene pass,
after the geometry. A cubemap is six square textures forming a cube, sampled by a 3D
**direction** rather than a 2D UV — exactly the shape "a colour per viewing direction" needs.
The chosen technique is the **cube-around-the-camera** one (over the fullscreen-triangle +
inverse-view-projection variant, which would need a `Mat4` inverse — added later in Step 19,
though this variant is still unbuilt).

```
   tools/gen_skybox.py  ─ 6 BMP faces ─►  loadCubemap → uploadCubemap (type CUBE, 6 layers, mips)
   (procedural day sky,                   recordSkybox (end of recordScene), per fragment:
    sun aligned to lights[0])               dir = normalize(cubeCornerPos)   (object pos = direction)
                                            skyColor = texture(uSky, dir) · kSkyIntensity
```

Four ideas make it sit right, all in `GpuRenderer` + `shaders/skybox.*`:

- **Sampled by direction.** The skybox reuses `makeCubeMesh`; its object-space corner position
  *is* the sample direction, forwarded from `skybox.vert` and interpolated per fragment — so no
  separate direction attribute is needed. Only the position attribute is declared (the full
  `Vertex` stride, mirroring the shadow pass).
- **Infinitely far.** `recordSkybox` zeroes the **translation** column of the view matrix
  (keeping only the camera's rotation) before combining with the projection, so the sky turns
  with the camera but never slides — unreachable.
- **Behind everything.** The vertex shader emits `gl_Position = clip.xyww`, pinning post-divide
  depth to `1.0` (far plane); the skybox pipeline tests **`LESS_OR_EQUAL`** with depth-write
  **off** and is drawn **last**, so the sky fills only pixels the scene left at the far-plane
  clear — no overdraw of shaded geometry.
- **Seam-free + tone-map-aware.** A dedicated **CLAMP_TO_EDGE** cubemap sampler stops the six
  faces seaming at their edges. The sky is rendered into the HDR target and later tone-mapped,
  so `skybox.frag` lifts it by a constant tuned to sit *below* the Step 10 **bloom threshold**
  (a real cross-system constraint — a too-bright sky blooms and washes out), while the sun disk
  is left bright enough to bloom on its own.

The cubemap upload path (`uploadCubemap`: `SDL_GPU_TEXTURETYPE_CUBE`, `layer_count_or_depth = 6`,
one copy region per face layer, a single mip-gen) is the reusable groundwork the next step —
**image-based lighting** — consumes to bake irradiance/prefiltered maps. The demo sky is
generated procedurally by `tools/gen_skybox.py` (same `uv`-run, BMP-only pattern as the texture
maps); loading real HDR/equirectangular skies is a later step.

### Image-based lighting (Step 15)

The skybox stops being a mere backdrop and starts **lighting the scene**. This fixes the flat
ambient term — the reason Step 12's metals looked dark: with only a constant fill and direct lights, a
metal (which has no diffuse) reflected almost nothing. IBL replaces the constant with the *actual
environment*, split into a diffuse and a specular part, each **precomputed once at load** into a small
texture so the per-pixel shader is just a few reads. The core is the **split-sum approximation**:

```
   loadCubemap → bakeIbl (one command buffer, at load):
     irradiance cube (32²)      ← irradiance_convolution.frag   (cosine-convolve the sky)
     prefilter cube (128², mips)← prefilter_env.frag            (GGX-blur the sky; roughness = mip)
     BRDF LUT (512², RG16F)     ← brdf_lut.frag  (baked in createIblResources — env-independent)

   triangle.frag ambient term (per pixel):
     diffuse  = irradiance(N) · albedo · kD
     specular = prefiltered(R, roughness) · (F0 · brdfLUT(N·V,roughness).x + .y)
```

The design mirrors the rest of the renderer:

- **Baked by rendering into cube faces.** `bakeIbl` draws the cube mesh six times per target (once
  per face; the prefilter also once per roughness **mip**) with a 90° camera down each axis — so
  **no `Mat4` inverse** is needed (the gap that kept Step 14 from the fullscreen-triangle skybox).
  `SDL_GPUColorTargetInfo`'s `layer_or_depth_plane` + `mip_level` select the subresource;
  `ibl_cube.vert` forwards the cube-corner position as the world sample direction.
- **Bake-at-load, not per-frame.** The two environment cubes are baked at the end of `loadCubemap`
  (so a sky reload re-bakes automatically); the environment-independent BRDF LUT is baked once in
  `createIblResources`. A `iblReady_` flag gates use until a bake has run; `iblEnabled_` (key `9`)
  is the runtime A/B toggle, passed to the shader in the light UBO's spare `ambient.w` lane.
- **CPU mirror + tests.** The precompute maths (Hammersley low-discrepancy sequence, GGX importance
  sampling, the **IBL** geometry term — `k = roughness²/2`, *not* the direct-lighting remap —
  `integrateBRDF`, `fresnelSchlickRoughness`) has a pure twin in `renderer/Pbr.hpp`, unit-tested in
  `tests/test_ibl.cpp`, exactly as the D/G/F terms have since Step 12.
- **The orientation gotcha.** The six capture views must match the `samplerCube` sampling convention
  (standard cube-capture up-vectors, `cubeCaptureViews`), or reflections come out mirrored/seamed — a
  compile-clean-render-wrong bug caught only by a `KOI_CAPTURE` frame, like the Step 9 sampler swap.

### glTF PBR material import (Step 16)

Materials stop being hand-authored and start coming **from the file**. The Step 13 machinery (five
fragment samplers, TBN normal mapping, per-pixel MR/AO) was built and tested against *generated BMPs*;
this step feeds it a real asset — the Khronos **Damaged Helmet** — by importing the glTF material.

- **`loadModel` now returns geometry *and* appearance.** The return type is a `LoadedModel { mesh,
  material }` (`ModelLoader.hpp`). `loadGltf` reads `primitive.material` via cgltf into a `koi::Material`;
  `loadObj` still returns geometry only (`.mtl` import out of scope). The engine uses the file's material
  for the helmet while keeping its own showcase materials on the older sphere/torus.
- **Two libraries, clean split of work.** cgltf parses the JSON + buffers but does **not** decode images —
  it hands us the *compressed* PNG/JPG bytes. **stb_image** decodes them: embedded `.glb` images come from
  a `buffer_view` (`stbi_load_from_memory`, no filesystem); external `.gltf` images resolve their URI to a
  file. stb_image is a third single-header dep, its implementation compiled once in `ModelLoader.cpp`.
- **sRGB vs. linear — a format choice, not a pass.** Colour textures (base-colour, emissive) are authored
  in sRGB and must be linearised before shading; data maps (MR/normal/AO) are already linear and must not
  be touched. `uploadToGpuTexture` gained an `srgb` flag that selects `R8G8B8A8_UNORM_SRGB` (the GPU
  un-gammas on sample) for exactly the two colour slots. Same bytes uploaded — only the sampling differs.
- **Emissive — added, not reflected.** `Material` gained `emissive` + `emissiveFactor` (default 0 ⇒ no
  change, no shader branch). `triangle.frag` reads a 9th sampler (slot 8) and *adds* `emissive` after all
  shading, so it stays bright regardless of lights and its bright pixels feed the Step 10 **bloom**. The
  factor rides in a second `vec4` of the per-material UBO (still one uniform buffer, just larger).
- **Deferred (documented):** glTF **node hierarchy** (we import raw primitive geometry, so the demo
  rotates the Z-up helmet upright itself), multi-primitive/multi-material meshes, per-texture **samplers**,
  base64 data-URI images, and colour management beyond colour textures. The helmet `.glb` is downloaded at
  configure time (soft-fail) and gitignored.

### Engine / app separation (Step 17)

The demo stopped living *inside* the engine. Through Step 16, `Engine` built the scene, placed the
lights, spun the hub and read the keyboard — so `koi_core` couldn't be reused without editing the demo
out of it. Step 17 draws the **public API boundary** the *Path to 1.0* track calls the defining
production milestone.

- **Inversion of control.** The frame loop stays in the engine (only it can acquire a swapchain image,
  pace to the display and submit), but its *contents* are the app's. So `Engine::run(Application&)` calls
  *back* into the app's hooks — `onStart` / `onUpdate(dt)` / `onEvent` / `frameView` / `onShutdown` — on an
  object it knows nothing concrete about. `Engine` now holds no `Node`, `Camera`, `Light` or `PostSettings`
  at all; it exposes **services** (`renderer()`, `requestQuit()`) the hooks reach back for.
- **`Application`** ([src/core/Application.hpp](src/core/Application.hpp)) is the interface; **`DemoApp`**
  ([samples/demo/DemoApp.cpp](samples/demo/DemoApp.cpp)) is the demo, now a `koi::Application` under
  `samples/` that owns the scene/camera/lights/post and holds the old `buildScene`/`setupLights`/animation/
  input code. `samples/demo/main.cpp` wires an `Engine` to a `DemoApp` in five lines — the whole contract a
  real client sees.
- **`FrameView`** ([src/renderer/FrameView.hpp](src/renderer/FrameView.hpp)) is the one value that crosses
  the boundary each frame: `{clearColor, view, root, cameraPos, lights, post}`. `frameView()` produces it;
  both `renderFrame(fv)` and `captureFrame(path, fv)` consume it, so the live and headless paths share a
  single "what to draw" definition (they previously took the same six args separately).
- **Lifetime.** Because the app now owns the GPU-backed scene but the engine owns the device, the app frees
  its scene in `onShutdown()` — which `run()` calls **while the renderer is still alive** — preserving the
  "meshes die before the device" rule across the new ownership split (see *Lifecycle & ownership*).
- **Behaviour-preserving.** Pure structural refactor: the `KOI_CAPTURE` frame is **byte-identical** to
  Step 16. The executable is now `koi-demo` (built from `samples/demo/`).

### Render queue & frustum culling (Step 20)

Through Step 19 the renderer **drew while it walked**: a recursive `recordNode` traversed the scene graph and
issued a GPU draw inline at every node, welding two jobs — *deciding what to draw* and *submitting it* —
together. That leaves no seam for the questions a scaling renderer must ask between "found an object" and "drew
it": is it on screen? can these draws be grouped by material? batched into one instanced call? sorted
back-to-front for blending? None are expressible on a recursion; all are natural on a **list**.

So Step 20 splits the frame into **traverse → list → submit**:

```
buildRenderQueue(root)  ──▶  vector<RenderItem>{ mesh, material, world, worldBounds }   (once per frame)
                                │
        ┌───────────────────────┴───────────────────────┐
   shadow pass: loop the WHOLE queue        scene pass: cull to camera Frustum, then submitItem each survivor
   (depth only — never camera-culled)       (skips off-screen draws entirely)
```

Design points worth recording:

- **`RenderItem`** is a flat, non-owning record (`RenderQueue.hpp`). Its `worldBounds` is the mesh's cached
  model-space `Aabb` run through `Aabb::transformed(world)` at build time, so per-frame culling is a plane test
  with no box rebuild. Because a shared mesh has one local box but many placements, the world box lives on the
  *item*, not the mesh.
- **Culling is a filter on the list**, using the Step 19 `Frustum::fromViewProjection(proj·view)` +
  `intersectsAabb`. It runs only for the **camera** pass. The **shadow** pass loops the whole queue —
  camera-culling a shadow *caster* would make its shadow blink in and out as it crosses the frustum edge (a
  caster the camera can't see may still shadow one it can). This asymmetry is the subtle correctness point the
  whole design protects; a later step can cull the shadow pass to the *light's* frustum instead.
- **The queue is a reused member** (`renderQueue_` / `visibleItems_` scratch) so no per-frame allocation; a
  `frustumCullingEnabled_` toggle (demo key `0`) + a logged drawn/total count make the win observable.
- **Behaviour-preserving.** The depth-first flatten preserves the original draw order and no math changed, so
  the `KOI_CAPTURE` frame is **byte-for-byte identical** to Step 19 — the proof the pivot changed no output.
- **Why it's a pivot, not just a feature:** sorting (by pipeline/material), instancing, transparency ordering,
  and deferred shading all operate on this list. It's the structural first move of the whole scaling track.

### Transparency & alpha blending (Step 21)

Every surface through Step 20 was **opaque**: the depth buffer alone resolved visibility, so draw order never
mattered (a nearer fragment simply *replaced* a farther one). A **translucent** surface instead *combines* with
what's behind it — the "over" operator `out = src·α + dst·(1−α)` — which is **order-dependent**, so it's the
first feature that has to consume the render queue's sortability.

Three things change, all small:

- **A second pipeline.** SDL bakes blend state + the depth-write flag into the immutable pipeline object, so a
  renderer that draws both needs two. `createTrianglePipeline` builds `transparentPipeline_` from the *same*
  shaders/layout as `trianglePipeline_`, flipping just two states: **blending on** (SRC_ALPHA / 1−SRC_ALPHA, ADD)
  and **depth-write off** (depth *test* stays LESS). Off-write is essential: a translucent fragment is still
  occluded by nearer opaque geometry, but must not stamp the depth buffer, or the translucent surfaces behind it
  would be depth-rejected and never blend through.
- **The shader emits a real alpha.** `triangle.frag` samples the albedo as a full `vec4` and outputs
  `outColor.a = material.w · albedoSample.a`, where `material.w` is the material's `opacity`. Opaque materials
  leave it at 1 and the opaque pipeline ignores alpha anyway, so existing objects render unchanged.
- **The pass splits into three, and the sky moves.** `recordScene` now runs **opaque → skybox → transparent**.
  `partitionByBlend` splits the culled `visibleItems_` by `Material::alphaMode`; the transparent bucket is sorted
  **back-to-front** (farthest first, by camera distance to the world-bounds centre) so each layer composites over
  the result so far. The skybox moves *before* the transparent pass — it writes no depth, so drawing translucent
  panes first would let the sky paint over them; resolving the background first lets glass blend over the real
  sky. Blending lands in the linear **HDR** target before tone-mapping, which is the correct place to mix colour.

```
cull → visibleItems ──▶ partitionByBlend ──┬─▶ opaque   (trianglePipeline_,   depth-write ON)
                                           │       ↓  then skybox (depth-write OFF, LEQUAL)
                                           └─▶ transparent, far→near (transparentPipeline_, depth-write OFF, blend)
```

`bindFrameLighting` factors the per-frame shadow/IBL sampler binds + light-uniform push so it can re-run after the
pipeline switch (a pipeline bind resets the pass's sampler bindings). A `transparentSortEnabled_` toggle (demo key
`T`) drops the sort to expose the mis-ordering artifact. **Documented deferrals:** translucent objects still cast
*solid* shadows (the shadow pass ignores materials); the per-object sort mis-orders interpenetrating meshes (→ OIT);
and the glTF loader still imports every model opaque. `partitionByBlend` is pure and unit-tested
(`tests/test_render_queue.cpp`).

### Debug draw (Step 22)

Steps 19–20 added spatial data — per-mesh `Aabb`s and the camera `Frustum` — that nothing put on screen. Debug
draw is the immediate-mode **line** overlay that does, following the same *app builds it → `FrameView` carries it →
renderer draws it* boundary as everything else:

- **A pure collector.** [`DebugDraw`](src/renderer/DebugDraw.hpp) turns shapes into a flat **line-list** vertex
  array (`DebugVertex { position, color }`) — `line`/`box`/`frustum`/`ray`/`cross`. `box` and `frustum` share a
  12-edge cube topology; `frustum` recovers world-space corners by unprojecting the **NDC cube** (x,y ∈ {−1,1},
  z ∈ {0,1}) through `inverse(viewProj)` — reusing the Step 19 `Mat4` inverse. No GPU state → unit-tested
  headlessly (`tests/test_debug_draw.cpp`).
- **Immediate mode + a transient buffer.** The app rebuilds the lines every frame and passes them via the new
  `FrameView::debugLines` span. `renderSceneAndPost` calls `uploadDebugLines` (before any render pass) which
  **recreates** the vertex buffer each frame — releasing the previous one, whose free SDL defers until the GPU is
  done, so no two frames share a buffer and nothing needs hand-synchronising.
- **An overlay pipeline.** `createDebugPipeline` builds a minimal unlit `debugLinePipeline_` — LINELIST topology,
  a 2-attribute (position + colour) layout, **depth-test on / write off** so lines are occluded by geometry but
  never disturb its depth, drawing into the HDR scene target so they also appear in `KOI_CAPTURE`. `recordDebug`
  binds it, pushes the camera `projView`, and issues one line-list draw at the *end* of `recordScene`.
- **Seeing the culler.** `GpuRenderer::lastCameraViewProjection()` exposes the exact matrix `recordScene` culls
  with, so the demo can **freeze** it and draw the frozen frustum — the Step 20 volume made visible. Demo keys:
  `G` (bounds), `L` (light icons), `F` (freeze + show the frustum); `KOI_DEBUG_DRAW` enables all three for a
  headless capture. **Documented deferrals:** lines are tone-mapped/FXAA'd (they draw into the HDR target); no
  x-ray (depth-off) mode; no text labels yet. With nothing queued, the overlay is a no-op and the frame is
  byte-identical to Step 21.

### HUD & text (Step 23)

The HUD is debug draw's 2D sibling — the same *app builds it → `FrameView` carries it → renderer draws it*
boundary — but it draws **text**, in **screen space**, and (crucially) in its **own pass after post-processing**:

- **A bitmap font in an atlas.** [`Font.hpp`](src/renderer/Font.hpp) embeds the public-domain **8×8** glyph table
  (8 bytes/glyph, bit 0 = leftmost pixel). At startup `createHudResources` **bakes** it into an RGBA **atlas**
  texture on a fixed 16×6 cell grid: a set bit → opaque white, clear → transparent (the alpha *is* the coverage).
  One reserved solid-white cell lets filled panels share the text pipeline. `Font.hpp` is the single source of
  truth for the layout, so the collector (computing UVs) and the bake (painting texels) can't disagree; `cellUV`
  applies a **half-texel inset** to stop nearest-filter sampling from bleeding across cell borders.
- **A pure collector.** [`Hud`](src/renderer/Hud.hpp) turns `text`/`rect` into a flat **triangle-list** of
  `HudVertex { pos, uv, color }` in **pixel** coordinates (top-left origin) — one 6-vertex quad per glyph, the pen
  advancing one cell per char, `\n`-aware, unknown chars → `?`. No GPU state → unit-tested headlessly
  (`tests/test_hud.cpp`).
- **Screen-space projection.** `hud.vert` maps pixels → clip space with a hand-written **orthographic** divide by
  the viewport size (pushed as a uniform), flipping Y (screen-down vs clip-up). `hud.frag` is just `atlas × tint`.
- **A final overlay pass, in LDR.** This is the key difference from debug draw. Debug lines live in the HDR scene
  pass (and so are tone-mapped/FXAA'd); the HUD instead draws in a **separate pass on the final image**, *after*
  `runPostChain`, with `LOAD_OP_LOAD` (keep the post-processed frame), **alpha blending**, and **no depth** — so
  glyphs stay pixel-crisp. `createHudResources` builds the textured `hudPipeline_` at the **swapchain (LDR)** format;
  `uploadHud` recreates the transient vertex buffer each frame; `recordHud` draws it. Because the pass targets the
  same `finalColor` for both the live and capture paths, the HUD appears in `KOI_CAPTURE` too.
- **In the demo.** `DemoApp::buildHud` fills a panel + title, a smoothed **FPS**/frame-time, the camera position,
  the debug-toggle states, and a controls legend; **`H`** toggles it, `KOI_HUD` enables it for a headless capture.
  **Documented deferrals:** fixed-width bitmap font only (no SDF/proportional), no scissor clip, ASCII only. With
  nothing queued the overlay pass is skipped and the frame is byte-identical to Step 22.

### Draw-call batching (Step 24)

The render queue (Step 20) existed to enable this: with drawing split into a flat *list*, the frame can now
**sort** that list to cut state changes and **instance** identical items into single draw calls. It's the engine's
first performance step, and it moves per-object transforms out of a uniform and into a per-instance vertex buffer:

- **Prepare, then submit.** `GpuRenderer::buildFrameBatches` runs the whole plan **before any render pass begins**
  (so the instance-buffer uploads, which each submit their own command buffer, finish before the passes draw). It
  sorts the whole queue **once** by `(mesh, material)` (`sortByMeshMaterial`) and derives *both* passes from that
  single sorted list: the shadow pass `coalesceBatches` it by mesh; the color pass culls it (survivors stay grouped)
  → `partitionByBlend` → `coalesceBatches` by `(mesh, material)`, then packs every visible transform into a transient
  **instance buffer**. The record passes (`recordScene`, `renderShadowPass`) then just bind + draw the prepared
  `DrawBatch`es. This deepens the *traverse → list → submit* split into *traverse → list → **batch** → submit*.
- **Instance-rate attributes.** The per-object matrices left the per-draw uniform and became **instance-rate vertex
  attributes** at a second buffer slot (advanced once per instance, not per vertex). `triangle.vert` reads
  `mat4 inModel` (locations 5–8) + `mat4 inNormalMatrix` (9–12) and its UBO shrinks to the shared `viewProj` (the MVP
  is formed in-shader as `viewProj * inModel`); `shadow.vert` reads `mat4 inModel` + a `lightViewProj` uniform. The
  color pipeline packs `InstanceData { model, normalMatrix }` (128 B); the depth-only shadow pipeline needs just the
  `model` matrix (64 B).
- **Two batch keys, two buffers.** The color pass coalesces the sorted list by **(mesh, material)** — a run must
  share the bound textures *and* the geometry. The shadow pass is depth-only, so it coalesces by **mesh alone**
  (material-blind → bigger runs) and, as before, is never camera-culled. Transparent items keep their back-to-front
  order (blending isn't commutative), so they are *not* material-batched — each draws as a single instance at its own
  slot in the color instance buffer. The two passes deliberately keep **separate** GPU instance buffers (color =
  visible-packed 128 B; shadow = whole-queue 64 B): merging them into one whole-queue buffer would leave the color
  pass to sub-batch across the gaps left by culled items, **fragmenting color batches under culling** — a regression
  for scattered-instance scenes. Only the CPU-side prep (the sort + the sorted list) is shared, not the buffers.
- **Correctness + measurement.** Opaque reordering is safe (the depth buffer resolves visibility), so the image is
  **visually identical** to Step 23 — only a handful of pixels differ, from float rounding under the new draw order.
  `GpuRenderer::lastDrawStats()` exposes items-vs-draw-calls, which `DemoApp::buildHud` shows as a live **Draws N
  (M items)** line. **Documented deferrals:** CPU-side batching only (no GPU-driven/indirect culling, deferred
  shading, or render graph yet); transparent objects still draw one-per-call.

## Lifecycle & ownership

Startup builds bottom-up, shutdown tears down top-down — the standard RAII
ordering that keeps resources valid for exactly as long as something uses them:

```
init:       SDL_Init → Window → GpuRenderer (claims Window)
run(app):   app.onStart (build scene + loadTexture: upload meshes + images) → loop → app.onShutdown (free scene)
shutdown:   GpuRenderer (releases Window) → Window → SDL_Quit
```

Two ordering constraints (Step 17 splits *where* each is enforced):
`GpuRenderer` must be destroyed before `Window` (it holds the window's swapchain) —
enforced by `Engine::shutdown()`, which resets `renderer_` then `window_`. And the
**app's scene + textures** must be destroyed before `GpuRenderer` (each `Mesh` and
`Texture` frees its GPU resource through the renderer's device — see the lifetime rule
above); since the app now *owns* the scene, it frees it in **`onShutdown()`**, which
`Engine::run` calls **while `renderer_` is still alive**. `DemoApp::onShutdown` resets
its `sceneRoot_`; the engine then tears the device down safely.

> One in-renderer exception (Step 14): the skybox **cube mesh** is a `shared_ptr<Mesh>`
> *member* of `GpuRenderer`, not part of the scene. Since member destructors run only
> *after* `~GpuRenderer`'s body — by which point the device is already destroyed — the
> destructor explicitly `.reset()`s `skyboxMesh_` (and releases the cubemap/sampler/pipeline)
> **while the device is still alive**, preserving the same "meshes die before the device" rule.
> The Step 15 IBL resources (three bake pipelines, the trilinear sampler, and the irradiance /
> prefilter / BRDF-LUT textures) are plain owned handles released the same way, alongside the skybox.

> Implementation note: `Engine`'s constructor and destructor are **defined in the
> `.cpp`**, not the header. Its `unique_ptr` members point to forward-declared
> types (`Window`, `GpuRenderer`); `unique_ptr`'s destructor needs the *complete*
> type, which only exists in the `.cpp`. This is the standard forward-declaration
> + `unique_ptr` pattern.

## Build system

CMake (≥ 3.28) with `CMakePresets.json` for one-command builds.

- `koi_core` — static library with all **engine** code under `src/` (no demo since
  Step 17). Building the engine as a library lets both the sample app and the test
  runner link the **same** code, and makes the engine/app boundary a real link boundary.
- `koi-demo` — the **sample** application executable (`samples/demo/main.cpp` +
  `samples/demo/DemoApp.cpp`, linking `koi_core`), depends on `koi_shaders` + `koi_assets`
  so compiled shaders and assets sit next to it. Step 17 renamed this from `koi-engine`
  and moved its sources out of `src/`.
- `koi-tests` — doctest runner (`koi_core` + the doctest header); Step 17 added
  `tests/test_application.cpp` (the `Application`/`FrameView` boundary).
- `koi_warnings` — an INTERFACE target carrying strict warning flags, applied to
  our code only (not to SDL3 / doctest).
- `koi_shaders` — a custom target that compiles `shaders/*` to `build/shaders/`
  via `glslc` + `spirv-cross` (see "Shaders & the build-time toolchain" above).
- `koi_assets` — a custom target that copies `assets/*` (e.g. `uv_grid.bmp`) to
  `build/assets/` next to the binary, so textures are found at runtime via
  `SDL_GetBasePath()` (the same convention as the compiled shaders).

**Dependencies:** SDL3 via `find_package(SDL3 CONFIG)` (Homebrew ships the CMake
package). doctest is fetched as a single header at configure time (we download the
header directly rather than building doctest's CMake project, whose minimum
version is too old for CMake 4.x). Step 9's model loaders — **tinyobjloader** and
**cgltf** — plus Step 16's image decoder **stb_image** are fetched the same way (single
headers into `build/_deps/models`, stb_image pinned to a commit); the one TU
(`ModelLoader.cpp`) that implements all three is compiled warning-free. Step 16 also
downloads the **Damaged Helmet** `.glb` at configure time (a soft-fail: the engine warns
and continues if it's absent) — gitignored, not committed. Shader compilation needs
`glslc` (shaderc) and `spirv-cross` on `PATH` (`brew install shaderc spirv-cross`);
MSL is generated with `--msl-decoration-binding` so resource indices follow the GLSL
`binding=` decorations (matching SDL's slot-based binding).

Generator is **Unix Makefiles** (always available); install Ninja for faster
builds if you like.

## Conventions

- **Namespace `koi`** wraps all engine code.
- **Logging:** use `KOI_INFO/WARN/ERROR/DEBUG` (never raw `printf`/`std::cout`).
  After any SDL call that returns null/false, log `SDL_GetError()`.
- **Headers** include the SDL headers they need and use `#pragma once`; include
  paths are rooted at `src/` (e.g. `#include "core/Engine.hpp"`).
- **Testing hook:** `KOI_MAX_FRAMES` env var runs a fixed number of frames then
  exits cleanly — used for headless smoke tests.
- **Visual debugging:** `KOI_CAPTURE=<path.bmp>` renders one frame to an off-screen
  texture, downloads it, and saves a BMP, then exits (`GpuRenderer::captureFrame`).
  This is the preferred way to verify rendering output — no screen capture needed.
- **Shaders:** authored in GLSL under `shaders/`; never hand-write MSL/SPIR-V —
  the build generates those. Compiled output is loaded relative to the executable
  (`SDL_GetBasePath()` + `shaders/`).

## Planned layering (as features land)

Future milestones slot into this structure without reshaping it:

- **Step 2 (done):** `renderer/` grew GPU buffer/transfer helpers (`uploadToGpuBuffer`)
  and a `Vertex` type; geometry moved from the shader into vertex + index buffers with
  a described vertex layout.
- **Step 3 (done):** a header-only `math/` module (hand-rolled `Vec`, `Mat4`) feeds an
  MVP matrix to the shader via a uniform buffer; a depth buffer joined the render pass.
  (Quaternions were deferred — no use case yet — per the "introduce when needed"
  principle; they finally arrived in Step 18.)
- **Step 4 (done):** `scene/` arrived with a `Camera`; the identity View matrix
  became a real `lookAt` camera flown with keyboard/mouse, and a delta-time clock
  drives motion. `Mat4` gained `lookAt`. The renderer now takes a `view` and draws a
  hardcoded cube cluster via `recordScene`.
- **Step 5 (done):** geometry moved into reusable `Mesh` objects (made by the
  renderer's new `createMesh` factory; shapes in `Primitives`), and the hardcoded list
  became a **scene graph** — `scene/Transform` + `scene/Node` with parent/child world
  transforms. `Mat4` gained `scaling`. `renderFrame`/`captureFrame` now take a scene
  root and `recordScene` traverses it. (The `begin/draw/end` split was deferred.)
- **Step 6 (done):** `Vertex` grew a **uv**; a `Texture` type plus
  `loadTexture`/`uploadToGpuTexture` and a shared sampler joined `renderer/`; the fragment
  shader samples a texture (bound at `set=2`); CMake's `koi_assets` copies the image asset.
  The cube split to 24 vertices for per-face UVs.
- **Step 7 (done):** `Vertex` grew a **normal**; the vertex shader gained the `model`
  matrix (to light in world space) and the fragment shader a **Blinn-Phong** light at a
  fragment uniform buffer (`set=3`), pushed per frame with the camera position. Per-face
  normals fell out of the Step 6 24-vertex cube for free.
- **Step 8 (done):** a per-object **`Material`** (texture + specular params) replaced the
  single global texture; the fragment shader gained a second uniform buffer (material at
  `set=3,b=1`), bound per draw. Nodes now carry shape + placement + appearance.
- **Step 9 (done):** **model loading** (OBJ via tinyobjloader, glTF via cgltf → `Mesh`,
  with 32-bit indices) and **shadow mapping** (a depth pass from the light's orthographic
  view, sampled in the main pass with bias + PCF). `renderFrame` now runs two passes.
- **Step 10 (done):** **post-processing** — the scene renders into an offscreen **HDR** target,
  then a chain of **fullscreen passes** (bloom = bright-pass + separable blur; a composite that
  applies exposure + **ACES tone-mapping** + vignette + **gamma**; then **FXAA**) writes the
  final image. Added `renderer/PostProcess.hpp` (`PostSettings` + pure helpers), five shaders,
  and the shared `renderSceneAndPost` path; effects toggle at runtime.
- **Step 11 (done):** **multiple lights** — a header-only `scene/Light.hpp` (directional/point/
  spot + pure attenuation/spot-cone helpers) replaced the single hardcoded sun; the fragment
  shader loops over a fixed-size std140 light array, the `Engine` owns + animates + toggles a
  `std::vector<Light>` threaded through the render calls, and only the sun still casts a shadow.
- **Step 12 (done):** **PBR materials** — Blinn-Phong shading replaced by a Cook-Torrance
  metallic-roughness BRDF. `Material` now carries `metallic`/`roughness`; the fragment shader's
  per-light body uses GGX/Smith/Fresnel with energy conservation; pure BRDF helpers live in
  `renderer/Pbr.hpp` (unit-tested). No vertex/texture/binding changes — the material uniform
  lanes were repurposed.
- **Step 13 (done):** **texture &amp; normal maps** — a per-vertex `tangent` joined `Vertex` (with
  pure TBN math in `renderer/Tangents.hpp`), `Material` gained optional metallic-roughness / normal /
  AO map handles (with neutral 1×1 fallbacks so map-less materials are unchanged), the fragment shader
  reads five samplers and perturbs the normal via the TBN basis, and textures gained **mipmaps** +
  **anisotropic filtering**. No BRDF change — only the inputs became per-pixel.
- **Steps 14–16 (done):** a **skybox** / environment cubemap, **image-based lighting** (irradiance +
  prefilter + BRDF-LUT bakes so the environment lights surfaces and metals reflect it), and **glTF PBR
  material import** (the Damaged Helmet) — each detailed in its own section above; all slotted into this
  layering without reshaping it.
- **Step 17 (done):** **engine/app separation** — the first *structural* rather than rendering step. The
  demo lifted out of `koi_core` into a `samples/demo/` **`koi::Application`**, with the engine driving it by
  inversion of control through the `Application` hooks + a `FrameView` bundle (see the section above). The
  layering was unchanged; the demo simply moved to the other side of a public API boundary.
- **Step 18 (done):** **quaternions** — a hand-rolled unit `Quat` joined `src/math/`, and `Transform`'s
  rotation changed from a `Vec3` of Euler angles to a `Quat` (no gimbal lock; `slerp`-able). `Quat::fromEuler`
  reproduces the old `Rz·Ry·Rx` matrix, so the swap was behaviour-preserving (the capture is visually
  identical). The camera stayed on yaw/pitch by design. This is the math prerequisite for skeletal animation.
- **Next:** cross-platform **CI + golden-image** tests, then the learning thread continues with
  **transparency**, more shadow casters (sun cascades, point-light cube maps), and eventually
  deferred/clustered shading — all slot into this same layering. See [`ROADMAP.md`](ROADMAP.md) for the
  sequenced plan and the wider engine-systems tracks (animation, physics, audio, tooling, gameplay).
