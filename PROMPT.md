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
