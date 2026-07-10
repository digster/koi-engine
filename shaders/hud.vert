// ============================================================================
//  hud.vert — vertex shader for the 2D screen-space HUD overlay (Step 23)
// ----------------------------------------------------------------------------
//  The HUD lives in PIXELS, not the 3D world. Its vertices arrive already placed
//  on the screen (top-left origin, x right, y down) by the CPU-side Hud collector
//  — a glyph quad, a panel rect. There is no camera, no model matrix, no
//  perspective: this shader's whole job is to convert a pixel position into
//  CLIP SPACE, the [-1,+1] cube the GPU rasterizes into.
//
//  That conversion is an ORTHOGRAPHIC (parallel) projection, and it's simple
//  enough to write by hand instead of a matrix: divide by the viewport size to
//  get 0..1, scale to -1..+1, and FLIP Y because screen-y grows downward while
//  clip-space +Y is up. The viewport size is pushed once per frame as a uniform,
//  so the same geometry lands correctly at any window size.
// ============================================================================
#version 450

// The HUD vertex layout — must match koi::HudVertex (pos, uv, colour) and the
// pipeline's vertex attributes in GpuRenderer::createHudPipeline.
layout(location = 0) in vec2 inPos;    // pixel position (top-left origin, y down)
layout(location = 1) in vec2 inUV;     // atlas texture coordinate in [0,1]
layout(location = 2) in vec4 inColor;  // RGBA tint

// Window size in pixels, pushed via SDL_PushGPUVertexUniformData (VERTEX-stage
// uniform buffer → set 1, binding 0), exactly like the skybox/debug shaders.
layout(set = 1, binding = 0) uniform HudUBO {
    vec2 viewport;
};

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
    vUV    = inUV;
    vColor = inColor;

    // Pixel → NDC. x: [0,w] → [-1,+1]. y: [0,h] downward → [+1,-1], so pixel
    // (0,0) maps to the TOP-left corner of the screen and y grows down as a HUD
    // author expects. z=0 puts the overlay on the near plane; w=1 (no perspective).
    vec2 ndc = vec2(inPos.x / viewport.x * 2.0 - 1.0,
                    1.0 - inPos.y / viewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
