// ============================================================================
//  triangle.vert — vertex shader for Step 1's "first triangle"
// ----------------------------------------------------------------------------
//  A vertex shader runs ONCE PER VERTEX. Its required job is to output a
//  position in "clip space" (gl_Position). The GPU then turns those positions
//  into pixels (rasterization) and runs the fragment shader for each pixel.
//
//  Normally the per-vertex data (positions, colors) arrives from a vertex
//  buffer. We don't have one yet — that's Step 2. Instead we bake three
//  positions and three colors right into the shader and pick one using
//  gl_VertexIndex (0, 1, 2 — the index of the vertex currently being processed).
//  This keeps Step 1 focused purely on shaders + the graphics pipeline.
// ============================================================================
#version 450

// Output to the fragment shader. "location = 0" is a slot number that must match
// the fragment shader's matching input. Values written here are interpolated
// across the triangle's surface before the fragment shader sees them — that
// interpolation is what produces the smooth color gradient.
layout(location = 0) out vec3 vColor;

// The three corners, expressed directly in Normalized Device Coordinates (NDC):
// x and y run from -1 (left/bottom) to +1 (right/top), with (0,0) at the center.
// Because we hardcode NDC here, no projection math is needed yet.
vec2 positions[3] = vec2[](
    vec2( 0.0,  0.5),   // top
    vec2( 0.5, -0.5),   // bottom-right
    vec2(-0.5, -0.5)    // bottom-left
);

// One color per corner. The classic red / green / blue so the interpolation is
// obvious.
vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),   // red
    vec3(0.0, 1.0, 0.0),   // green
    vec3(0.0, 0.0, 1.0)    // blue
);

void main() {
    // gl_Position is vec4 (x, y, z, w). z = 0 (on the near plane), w = 1 (no
    // perspective division yet).
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vColor = colors[gl_VertexIndex];
}
