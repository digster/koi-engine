// ============================================================================
//  debug_line.vert — vertex shader for immediate-mode debug lines (Step 22)
// ----------------------------------------------------------------------------
//  The simplest vertex shader in the engine, on purpose. Debug lines are already
//  in WORLD space (the CPU-side DebugDraw built them there — a box's corners, a
//  frustum's unprojected corners, a light's cross), so all this does is apply the
//  camera: multiply by the view-projection to land in clip space. There is no
//  model matrix (the vertices are pre-placed), no normals, no lighting — a debug
//  line is a flat colour you want to SEE, not shade.
//
//  The colour is carried straight through to the fragment shader; the rasterizer
//  interpolates it along the segment (both endpoints usually share it, so the
//  line is a single solid colour).
// ============================================================================
#version 450

// The debug vertex layout — must match koi::DebugVertex (position, then colour),
// and the pipeline's vertex attributes in GpuRenderer::createDebugPipeline.
layout(location = 0) in vec3 inPosition;  // world-space endpoint
layout(location = 1) in vec3 inColor;     // flat line colour

// projection · view, pushed once per frame via SDL_PushGPUVertexUniformData
// (VERTEX-stage uniform buffer → set 1, binding 0), exactly like the skybox.
layout(set = 1, binding = 0) uniform DebugUBO {
    mat4 viewProj;
};

layout(location = 0) out vec3 vColor;

void main() {
    vColor = inColor;
    gl_Position = viewProj * vec4(inPosition, 1.0);
}
