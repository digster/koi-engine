# CLAUDE.md — project preferences for Koi Engine

Guidance for AI assistants (and humans) working in this repo. These are the
project-specific preferences stated by the project owner; global preferences in
`~/.claude/CLAUDE.md` still apply on top of these.

## What this project is

- A **learning-focused 3D engine** built **from scratch** to understand how
  real-time graphics works — not a product.
- Language/stack: **C++ (C++20)** with **SDL3** and its cross-platform **GPU API**.
- Built **step by step**: start with the basics and keep adding more advanced
  functionality one milestone at a time. Don't jump ahead — each step should be
  small, runnable, and understood before moving on.

## Code

- **Document the important pieces thoroughly.** Comments should explain the *why*
  and the non-obvious *how*, specifically to improve a reader's understanding of
  the engine. Follow good commenting etiquette (don't narrate the obvious).
- Clean, performant, modern C++. RAII for resource ownership.
- All engine code lives under `namespace koi`.

## Documentation (`docs/`)

- Every step has a tutorial in `docs/`.
- **Format: standalone HTML, no build step.** Docs are plain `.html` files that
  open directly in a browser (`file://`) — no static-site generator, Markdown
  compiler, or bundler. Share one relatively-linked `docs/style.css`; keep it
  **offline-friendly** (no CDNs, web fonts, or JavaScript). New tutorials are
  authored as HTML following the existing pages, and `docs/index.html` links them.
- **Crucial:** the docs are written **as a guide for a graphics-programming
  beginner**, not merely as code commentary. Wherever a graphics concept appears
  (swapchain, command buffer, render pass, present, depth buffer, MVP matrix,
  projection, etc.), **explain what it is and why it exists from first
  principles** before showing how our engine uses it.
- Division of labor: **code comments teach the code; docs teach the concepts.**
- Keep docs in sync with the code as it changes.

## Technical decisions made

- **Renderer:** SDL3 **GPU API** (Metal/Vulkan/D3D12), chosen over OpenGL because
  OpenGL is deprecated on macOS and we want to learn modern GPU concepts.
- **Math:** **hand-rolled** (vectors, matrices, quaternions written ourselves) —
  arriving in Step 3. No GLM; the point is to understand the math.
- **Shaders:** authored once in **GLSL** under `shaders/`, compiled at build time
  via `glslc` (GLSL→SPIR-V) then `spirv-cross` (SPIR-V→MSL). Never hand-write
  MSL/SPIR-V. (We chose this over SDL's `shadercross`/HLSL because the local
  `shadercross` install is broken and GLSL is more beginner-friendly.) Remember the
  MSL entry point is `main0`, not `main`.

## Workflow

- Update `ARCHITECTURE.md` on any architectural change; keep `README.md` and
  `docs/` current.
- Tests live in `tests/` (doctest). Add tests with new features and run them.
- Append each prompt to `PROMPT.md`; write a session summary to `memory/YYYY-MM-DD.md`.
- Generate a commit message after each change; **do not commit automatically.**
