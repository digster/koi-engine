// ============================================================================
//  triangle.vert — vertex shader for Step 3's spinning 3D cube
// ----------------------------------------------------------------------------
//  A vertex shader runs ONCE PER VERTEX and must output a clip-space position.
//  Until now our vertices were already in clip space (flat 2D in NDC). Now they
//  arrive in 3D MODEL space and we transform them with a single matrix — the
//  "MVP" (Model * View * Projection) — to place, orient, and project them into
//  clip space. The GPU then divides by w (perspective division), which is what
//  makes farther parts of the cube look smaller.
// ============================================================================
#version 450

// --- Per-vertex inputs (from the vertex buffer) ----------------------------
// inPosition is now a vec3: the cube lives in real 3D space.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
// Step 6: a 2D texture coordinate per vertex. The rasterizer interpolates it across
// the triangle so each fragment knows where to look in the texture.
layout(location = 2) in vec2 inUV;

// --- Per-draw uniform (the same for every vertex this draw) ----------------
// A uniform buffer holds values constant across the whole draw call, unlike the
// per-vertex `in` attributes above. SDL3's GPU API places VERTEX-stage uniform
// buffers in descriptor set 1 (set 0 is for textures/storage); we upload this
// each frame with SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, ...). The mat4
// is laid out column-major, matching our koi::Mat4 storage.
layout(set = 1, binding = 0) uniform UBO {
    mat4 mvp;
};

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    // Promote the 3D position to homogeneous coordinates (w = 1, marking it a
    // point) and transform it into clip space. The GPU does the w-divide next.
    gl_Position = mvp * vec4(inPosition, 1.0);
    vColor = inColor;
    vUV = inUV;  // pass the texture coordinate straight through to the fragment stage
}
