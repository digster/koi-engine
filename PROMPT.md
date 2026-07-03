# Prompt Log

A running record of the prompts that shaped this project, for context.

---

## 2026-06-28

**Initial project request:**
> - In this repo, we are going to learn to make a 3D engine.
> - We will be using SDL3 and C++ for this.
> - We'll be making this engine step by step, starting with the basics, and then keep adding advanced functionality.
> - Make sure all the important code pieces are well documented, so as to improve the understanding.
> - Have a 'docs' folder inside the repo, which will have documentation and tutorials to help us understand our engine.
> - Make sure to fill the CLAUDE file in this project with the preferences I have mentioned.

**Follow-up guidance (during planning):**
> - Remember, the docs should not just explain the code but also be a guide from the point of view of a graphics beginner and explain important concepts where required.
> - Remember to add this preference to the project CLAUDE file as well.

**Decisions made (via clarifying questions):**
- Rendering backend: **SDL3 GPU API** (over OpenGL).
- 3D math: **hand-rolled** (over GLM).
- First step scope: **window + clear screen** (over also rendering a triangle).

**Docs format request:**
> The docs should be in HTML without requiring a build step.
> ... The html does not have to be dark aware.

**Next milestone request:**
> Okay, work on the next milestone.

**Decisions made (Step 1 — first triangle):**
- Shader language + toolchain: **GLSL via `glslc` + `spirv-cross`** (over HLSL +
  `shadercross`, whose install is broken locally).
- Triangle data: **baked into the vertex shader** (no vertex buffer until Step 2).

**Visual-debugging request:**
> For debugging visually, you can output the app's output to an image or texture
> and use that. Also add that as a reference in the project CLAUDE file.

Added `KOI_CAPTURE` (render one frame to a BMP via `GpuRenderer::captureFrame`);
referenced in `CLAUDE.md`.

**Next milestone request (Step 2):**
> Work on the next milestone.

**Decision made (via clarifying question):**
- Step 2 geometry: **a quad drawn with a vertex buffer + an index buffer** (over a
  triangle with only a vertex buffer), so the index buffer is genuinely meaningful
  (4 vertices reused 6 times).

**Next milestone request (Step 3):**
> Okay, work on the next step.

**Decisions made (via clarifying questions):**
- Cube motion: **auto-spin** (per-frame MVP from `SDL_GetTicks()`; the capture uses a
  fixed angle), over a static angled view.
- Quaternions: **deferred** until a milestone needs them, over implementing them now —
  following the project's "introduce a subsystem only when needed" principle (Step 3
  math is `Vec` + `Mat4` only). The view matrix is likewise deferred to Step 4.

**Next milestone request (Step 4):**
> Okay, work on the next step.

**Decisions made (via clarifying questions):**
- Look input: **mouse look** (SDL relative mouse mode + motion deltas) for yaw/pitch,
  over arrow-key look or both. Movement is WASD + E/Q regardless.
- Scene content: **a few static cubes** at fixed positions (a hardcoded list, not a
  scene graph), over a single cube — so flying the camera is legible.

**Next milestone request (Step 5):**
> Work on the next milestone.

**Decisions made (via clarifying question):**
- Demo scene shape: **minimal animated hierarchy** — a spinning hub cube with a few
  orbiting satellites, one carrying a grandchild "moon", plus a ground plane — over a
  richer solar-system scene or a static tree. Shows parent→child→grandchild inheritance
  with the least demo code (the engine abstractions are identical either way).
- Layering: `Mesh` placed in **`renderer/`** (it owns GPU buffers, made by the renderer's
  `createMesh`); `Transform` + `Node` in **`scene/`**, with `Node` referencing a mesh via
  `shared_ptr<Mesh>` — a documented refinement of ARCHITECTURE's earlier "scene/ grows a
  mesh type" note.
- Still **deferred** (per "introduce a subsystem only when needed"): quaternions
  (`Transform` uses Euler angles, like `Camera`) and the `renderFrame` begin/draw/end
  split. No shader/vertex-format change — lighting/textures are Step 6.

**Next milestone request (Step 6):**
> Okay, work on the next step.

**Decisions made (via clarifying question):**
- Scope: **textures first** (over textures-only-vs-lighting-first-vs-both). The roadmap's
  "Textures + Phong lighting" is two features with different infrastructure, so per the
  "small steps" principle it was split: **Step 6 Textures, Step 7 Phong lighting** (Models/
  shadows shifted to Step 8+). Roadmaps in README + docs updated.
- Layering (consistent with Step 5): `Texture` in **`renderer/`** (a GPU resource, made by
  the renderer's `loadTexture`); the **sampler** is renderer-owned reusable state; the
  *Engine* owns the `Texture` and passes it to the renderer each frame. One texture for the
  whole scene — per-mesh materials are a later step.
- Image: load a committed `assets/uv_grid.bmp` via `SDL_LoadBMP` (no SDL_image dep), copied
  next to the binary by a new CMake `koi_assets` target; sampler uses REPEAT so the floor
  tiles. Cube split 8→24 verts for per-face UVs.

**Next milestone request (Step 7):**
> Ok, work on the next step.

**Decisions made (no clarifying question — scope set by the Step 6 roadmap split):**
- Step 7 = **Phong lighting** (the second half of the split). A single fixed
  **directional** light (sun) with **Blinn-Phong** ambient + diffuse + specular, modulating
  the Step 6 texture as albedo. Point/multiple lights, attenuation, and per-object materials
  deferred to Step 8+.
- Structural consequence: lighting is in **world space**, so the vertex UBO grew to
  `{ mvp, model }` and the fragment shader got a light UBO at **`set=3`**; the camera
  position threads through `renderFrame`/`captureFrame` for specular. World normal uses
  `mat3(model)` (correct for the scene's uniform scale; inverse-transpose normal matrix
  deferred). The Step 6 24-vertex cube already supplied per-face normals.

**Next milestone request (Step 8):**
> Okay, work on the next step.

**Decision made (via clarifying question — materials vs model loading vs shadows):**
- Step 8 = **per-object materials** (over model loading or shadow mapping). A `Material`
  (`scene/Material.hpp`: texture + shininess + specStrength) is referenced per `Node` and
  bound **per draw**, retiring the single global texture + hardcoded shader constants.
  Completes the shape (`Mesh`) / placement (`Transform`) / appearance (`Material`) split.
- Fragment shader gained a **second** uniform buffer (material at `set=3,binding=1`, beside
  the light at binding 0); texture binding moved into `recordNode`. Added a second texture
  (`assets/dots.bmp`); demo uses 3 materials (matte floor, glossy cubes, extra-shiny hub —
  same texture, higher shininess). No vertex change. Model loading / shadows / post-fx
  shifted to Step 9+.

**Next milestone request (Step 9):**
> Okay, work on the next iteration.

**Decisions made (via clarifying questions):**
- Direction (over hand-rolled OBJ loading / shadows-only / post-fx-only): **use an external
  library for OBJ + glTF loading, and focus the from-scratch effort on shadow mapping +
  post-processing.** So model parsing is library-based; the graphics is hand-written.
- This iteration's scope (over model-loading-only / shadows-first): **model loading +
  shadow mapping bundled** into one larger step. Post-processing → Step 10.
- Library (over assimp / OBJ-only): **tinyobjloader + cgltf** (two single headers, downloaded
  at configure time like `doctest` — keeps the build light).
- Consequences: `Mesh` gained 16/32-bit index sizes; `Mat4` gained `orthographic`; a
  depth-only shadow pipeline + 2048² sampleable shadow map; two-pass render (shadow then main);
  a second fragment sampler. Models generated procedurally (sphere.glb, torus.obj) to avoid
  copyright. Hit + fixed a `spirv-cross` MSL sampler-swap via `--msl-decoration-binding`.

**Next milestone request (Step 10):**
> Work on the next step.

**Decision made (via clarifying question — effect scope):** user chose **all** candidate
effects, so Step 10 = the **full post-processing stack**.
- Architecture: the scene renders into an off-screen **HDR** target (`R16G16B16A16_FLOAT`)
  instead of the swapchain, then a chain of **fullscreen passes** processes it. Each pass draws
  a buffer-free **fullscreen triangle** (`fullscreen.vert`, from `gl_VertexIndex`).
- Effects: **bloom** (bright-pass `bright.frag` + separable Gaussian `blur.frag`, half-res
  ping-pong) → **composite** (`composite.frag`: exposure, **ACES** tone-map, vignette,
  linear→sRGB **gamma**) → **FXAA** (`fxaa.frag`, last, on the gamma-encoded LDR image). Order
  is dictated by colour space (bloom in HDR-linear, FXAA in sRGB).
- Structure: main pipeline's colour format switched swapchain→HDR; `GpuRenderer` gained four
  fullscreen pipelines + a clamp sampler + lazily-resized targets (`ensureSceneTargets`), and a
  shared `renderSceneAndPost` used by both `renderFrame` and `captureFrame` (no drift). New
  `renderer/PostProcess.hpp` holds `PostSettings` + pure helpers (unit-tested in `test_post.cpp`).
  Effects toggle live (keys `1`–`4`, `[`/`]` exposure). Multiple lights / PBR → Step 11+.

**Next milestone request (Step 11):**
> Work on the next step.

**Decision made (via clarifying question — which slice of "Step 11+"):** user chose
**multiple lights** (over PBR / normal mapping). So Step 11 = point + spot lights alongside the
directional sun.
- New `scene/Light.hpp` (header-only, SDL-free): a `Light` struct (type = directional/point/spot,
  position, direction, colour, intensity, range, spot-cone cosines, `enabled`) + pure helpers
  `attenuation` (windowed inverse-square), `spotFactor` (smoothstep cone), `activeLightCount`
  (mirroring the renderer's pack/cap), unit-tested in `tests/test_light.cpp`.
- Shader: `triangle.frag`'s single light became a fixed `MAX_LIGHTS=8` **std140 array** + a loop
  (per-type `L`/attenuation, spot cone; ambient once; **shadow only on light 0, the sun**). Each
  `GpuLight` packed as 4×`vec4` (64 B) so C++/GLSL agree byte-for-byte. Vertex shader unchanged.
- Renderer: light list threaded as `std::span<const Light>` through `renderFrame`/`captureFrame`
  → `renderSceneAndPost` → `recordScene`; the sun's direction now comes from `lights[0]` and
  drives both the shadow map and the UBO. `computeLightViewProj` takes that direction.
- Engine: owns `std::vector<Light> lights_` (sun + 2 point + 1 spot), orbits one point light,
  toggles groups with keys `5`/`6`/`7`. Only the sun casts a shadow (cube maps/cascades → later).
- Verified: clean build under strict warnings; 46 tests pass; headless smoke (no GPU validation
  errors); A/B capture (all lights vs sun-only) confirms each light contributes, shadows intact.
  PBR / normal maps / more shadow casters → Step 12+.

**Next milestone request (Step 12):**
> Work on the next milestone.

**Decision made (via clarifying question — which slice of "Step 12+"):** user chose **PBR
materials** (over normal mapping / better shadows). So Step 12 = Cook-Torrance metallic-roughness,
replacing Blinn-Phong.
- `scene/Material.hpp`: `shininess`/`specStrength` → `metallic` + `roughness` (both 0..1); albedo
  texture unchanged.
- New `renderer/Pbr.hpp` (header-only, SDL-free): pure `distributionGGX` (D), `geometrySmith`/
  `geometrySchlickGGX` (G), `fresnelSchlick` (F), `kPi` — mirrors the shader, unit-tested in
  `tests/test_pbr.cpp`.
- `triangle.frag`: per-light Blinn-Phong body swapped for Cook-Torrance `spec = D·G·F/(4·NdotV·NdotL)`,
  `F0 = mix(0.04, albedo, metallic)`, `kd = (1-F)(1-metallic)`, Lambert `albedo/π`. Vertex shader,
  light loop, and the material uniform's binding/size all UNCHANGED (only the x/y lanes repurposed
  → no `loadShader` edit). Ambient stays a crude `ambient·albedo` fill (metals dark without IBL).
- `recordNode` pushes `{metallic, roughness}`. `buildScene` materials retuned to showcase metal vs
  dielectric + rough vs smooth (metal sphere/hub, dielectric floor/torus). `setupLights` intensities
  bumped (sun 1→3, points 9→24, spot 14→40) to offset the energy-conserving `/π`.
- Also fixed `tests/test_node.cpp` (used the removed shininess/specStrength fields).
- Verified: clean build under strict warnings; **49 tests pass**; headless smoke (no GPU validation
  errors); capture shows the metal sphere as a dark, albedo-tinted metal with a sharp bright
  reflection (vs Step 11's matte sphere), glossy dielectric torus, shadows intact. Texture maps /
  IBL / more shadow casters → Step 13+.

**Roadmap request:**
> Create a roadmap for the engine and save it in ROADMAP.md

**Decisions made (via clarifying questions):** phased horizon with non-graphics engine systems as
**first-class** tracks (not a "someday" tier); **recap** the completed steps AND **remove the roadmap
table from `documentation/docs/index.html`**; number the next few steps then theme the rest.
- New **`ROADMAP.md`** (repo root): records the **Step N ↔ `documentation/docs/(N+1)-*.html`** numbering offset
  (`00-getting-started` is a prerequisites page → next is Step 13 = `documentation/docs/14-*.html`); recap table of
  Steps 0–12; numbered next milestones **13 texture & normal maps → 14 skybox/env maps → 15 IBL**
  (each with goal / concepts / files-touched / why-here, in strict dependency order); then first-class
  themed tracks — Rendering (shadows, scaling/deferred, image quality, geometry) and Engine systems
  (math/quaternions, animation, scene/ECS, physics, audio, input, UI/tools, gameplay, platform) —
  plus an exploratory tier, a suggested dependency-ordered path, and a "how steps get added" section.
- **Single source of truth:** removed the `<table>` from `documentation/docs/index.html` (kept the `Roadmap`
  heading + a pointer to `../ROADMAP.md`, matching the existing `../README.md`/`../ARCHITECTURE.md`
  links); replaced `README.md`'s duplicate table with a pointer; redirected `ARCHITECTURE.md`'s
  "Step 13+" bullet to ROADMAP.md while keeping its architectural claim.
- Verified: **0 `<table>`** left in `index.html`; all 14 repo-relative links in ROADMAP.md resolve;
  docs-only change (no C++/shader/CMake) → build + 49-test suite unaffected.

**Roadmap review request (2026-07-02):**
> At some point I want to take this engine to a professional level and use it for production apps.
> So from this pov have a look at the current ROADMAP.md file and see if any improvements can be
> made. Don't change anything at this point, just let me know the improvements.

**Outcome:** analysis only (no ROADMAP.md changes) — a written review of production-readiness gaps:
engine/app separation, architectural pivots to flag early (threading, render queue, shader variants,
memory strategy), promoting cross-platform CI + golden-image regression testing, asset pipeline,
robustness/fuzzing, and a "definition of 1.0" section.

**Roadmap update request (2026-07-02):**
> Also think of some additions other than those mentioned above (does not have to be related to
> productionizing the engine). Then combine and update all the recommendations to the roadmap.

**Outcome:** `ROADMAP.md` restructured (docs-only). New third guiding principle ("learning first,
production eventually"); Step 13 gains mipmaps/aniso + Damaged Helmet verification; new
**Architecture pivots** section (render queue, threading, shader variants, memory, render graph);
new **Transparency & blended effects** subsection (blending/sorting, cutout, particles, OIT);
Image quality gains emissive/sRGB/reversed-Z/fog; Geometry gains terrain+noise and standard test
scenes; Math gains a geometry utility layer (AABB/ray/plane/frustum); Physics gains a fixed-timestep
prerequisite; UI & tools gains debug draw, shader hot-reload, CVars; **Platform & build replaced by
a "Path to 1.0 — production hardening" track** (engine/app separation, golden-image CI, asset
cooking, robustness/fuzzing, packaging); new **Definition of 1.0** quality-gates checklist;
suggested-path diagram reworked; hot-reload duplication removed.

**Tooling + docs relocation (2026-07-02):**
> * add a clangd file so that code completion works in vscode
> * Move the 'docs' folder inside a 'documentation' folder and update any references

**Outcome:** Added a root `.clangd` (`CompileFlags.CompilationDatabase: build`) pointing clangd at
the CMake-generated `build/compile_commands.json` (already emitted via `CMAKE_EXPORT_COMPILE_COMMANDS`),
so VSCode's clangd extension gets accurate IntelliSense. Moved `docs/` → `documentation/docs/` via
`git mv` (history preserved) and rewrote every reference: the docs' own **124 outward `../` links
deepened to `../../`** (folder now one level lower), `docs/` → `documentation/docs/` across README,
ROADMAP, ARCHITECTURE, CLAUDE.md, PROMPT.md and `memory/*.md`, and 7 path-like code comments in
`src/`+`shaders/` (prose "docs" mentions left intact). Verified: 346/346 internal doc links resolve,
0 broken; build + doctest suite pass unchanged.

**Step 13 — texture & normal maps (2026-07-02):**
> Pick up something from the roadmap file to work on next.
> [scope chosen via clarifying question: "+ metallic/roughness/AO maps" — the fuller per-pixel slice]

**Outcome:** Implemented **Step 13** (texture maps + normal mapping + mipmaps/anisotropy). A
per-vertex `tangent` joined `Vertex` (pitch 44→56; pure TBN math in new `renderer/Tangents.hpp`,
unit-tested in `test_tangent.cpp`) — hardcoded for primitives, computed (Lengyel) or read from glTF
`TANGENT` for loaded models. `Material` gained optional `metalRough`/`normalMap`/`ao` handles (scalars
became glTF-style factors); a material omitting a map binds a neutral 1×1 fallback (white / flat
normal) so map-less materials render exactly as Step 12 — no shader permutation. The fragment shader
now reads **5 samplers** (4 per-material maps at slots 0–3, shadow at 4), builds the TBN and perturbs
the normal, and samples per-pixel metallic-roughness/AO; the Cook-Torrance BRDF is unchanged.
`uploadToGpuTexture` generates a mip chain (`SDL_GenerateMipmapsForGPUTexture`, `SAMPLER|COLOR_TARGET`)
and the shared sampler enables anisotropy. Demo maps generated by new `tools/gen_textures.py` (uv +
Pillow); the hub cube showcases the metal/dielectric checker + relief, the floor shows relief/AO +
shimmer-free recede. Deferred (roadmap-separate): glTF material/texture import, PNG loading, Damaged
Helmet. Verified: build clean under strict warnings, 54 doctest cases pass, `KOI_MAX_FRAMES` shows no
GPU validation errors, `KOI_CAPTURE` confirms all effects. New tutorial
`documentation/docs/14-texture-and-normal-maps.html` + nav/index links; README/ARCHITECTURE/ROADMAP updated.

**Step 14 — skybox & environment cubemap (2026-07-02):**
> Pick something from the roadmap to work on next.
> [technique chosen via clarifying question: cube-mesh skybox (over fullscreen-triangle); day-sky look]

**Outcome:** Implemented **Step 14** (skybox & environment cubemap). Added a **cubemap** upload path
(`GpuRenderer::uploadCubemap` → `SDL_GPU_TEXTURETYPE_CUBE`, `layer_count_or_depth = 6`, one copy region
per face layer + a single mip-gen; `loadCubemap` loads six BMP faces the same way `loadTexture` loads
one) and a **skybox pipeline** (`shaders/skybox.vert`/`.frag`) that draws a unit cube around the camera
(reusing `makeCubeMesh`, position attribute only — mirroring the shadow pass). The cube's object-space
corner *is* the sample direction. Three rendering tricks: the view's **translation is stripped** (sky
stays infinitely far), depth is **pinned to the far plane** (`gl_Position = clip.xyww`) with a
**`LESS_OR_EQUAL`** test + depth-write off, and the sky is **drawn last** in `recordScene` so it fills
only background pixels. A dedicated **CLAMP_TO_EDGE** cubemap sampler avoids face seams; the skybox
cube mesh is a renderer member explicitly `.reset()` before device teardown. The demo sky is a
procedural **day sky** from new `tools/gen_skybox.py` (uv + Pillow + numpy): a zenith→horizon gradient,
a ground tint, and a sun disk aligned to the scene's sun light (`lights[0]`), authored by inverting the
standard cube-face direction convention so the six faces are seamless. Tuning story: the sky first
washed out because its brightness crossed the Step 10 **bloom threshold (0.9)** — deepened the palette
and set `kSkyIntensity = 1.25` so the sky body stays sub-bloom while the sun disk still glows. Runtime
toggle on key `8`. Deferred (roadmap): the fullscreen-triangle + inverse-VP variant (needs a `Mat4`
inverse), real HDR/equirectangular skies, and IBL (Step 15). Verified: build clean under strict
warnings (skybox shaders compile GLSL→SPIR-V→MSL), 54 doctest cases pass, `KOI_MAX_FRAMES` shows no GPU
validation errors, `KOI_CAPTURE` confirms a seamless sky behind all geometry with the sun blooming. New
tutorial `documentation/docs/15-skybox-and-cubemaps.html` + nav/index links (529/529 doc links resolve);
README/ARCHITECTURE/ROADMAP updated.
