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
