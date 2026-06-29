// ============================================================================
//  triangle.vert — vertex shader for Step 2's vertex/index-buffered quad
// ----------------------------------------------------------------------------
//  A vertex shader runs ONCE PER VERTEX. Its required job is to output a
//  position in "clip space" (gl_Position). The GPU then turns those positions
//  into pixels (rasterization) and runs the fragment shader for each pixel.
//
//  In Step 1 the per-vertex data was baked into this shader and picked with
//  gl_VertexIndex. In Step 2 the data instead arrives from a real GPU *vertex
//  buffer*: each invocation receives one vertex's attributes through the
//  `in` variables below. This is how every real engine feeds geometry — the
//  shader stops knowing the geometry and just transforms whatever it is handed.
// ============================================================================
#version 450

// --- Per-vertex inputs (read from the bound vertex buffer) -----------------
// "location = N" is the attribute slot. It is the single source of truth that
// must line up across FOUR layers, or the GPU reads garbage (often silently):
//   GLSL  layout(location=N)  ->  SPIR-V decoration  ->  MSL [[attribute(N)]]
//   ->  SDL_GPUVertexAttribute.location  (set when we build the pipeline).
// Our C++ koi::Vertex struct supplies these: position at byte offset 0 (FLOAT2),
// color at byte offset 8 (FLOAT3). See src/renderer/Vertex.hpp.
layout(location = 0) in vec2 inPosition;   // x, y in Normalized Device Coords
layout(location = 1) in vec3 inColor;      // r, g, b for this corner

// Output to the fragment shader. "location = 0" here must match the fragment
// shader's matching input. Values written here are interpolated across the
// surface before the fragment shader sees them — that produces the gradient.
layout(location = 0) out vec3 vColor;

void main() {
    // gl_Position is vec4 (x, y, z, w). z = 0 (on the near plane), w = 1 (no
    // perspective division yet — that arrives with the projection matrix in
    // Step 3). We simply promote our 2D NDC position into clip space.
    gl_Position = vec4(inPosition, 0.0, 1.0);
    vColor = inColor;
}
