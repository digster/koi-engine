// ============================================================================
//  triangle.vert — vertex shader (3D transform + lighting inputs)
// ----------------------------------------------------------------------------
//  A vertex shader runs ONCE PER VERTEX and must output a clip-space position.
//  Vertices arrive in 3D MODEL space; the "MVP" (Model * View * Projection) matrix
//  places, orients, and projects them into clip space (the GPU then divides by w —
//  perspective division — making farther things smaller).
//
//  Since Step 7 it also prepares LIGHTING: it outputs each vertex's WORLD-space
//  position and normal (using the separate `model` matrix), which the fragment
//  shader needs to shade in world space. See documentation/docs/08-lighting-and-normals.html.
// ============================================================================
#version 450

// --- Per-vertex inputs (from the vertex buffer) ----------------------------
// inPosition is now a vec3: the cube lives in real 3D space.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
// Step 6: a 2D texture coordinate per vertex. The rasterizer interpolates it across
// the triangle so each fragment knows where to look in the texture.
layout(location = 2) in vec2 inUV;
// Step 7: the surface normal — the unit direction the surface faces, in model space.
// Lighting compares it against the light/eye directions to decide brightness.
layout(location = 3) in vec3 inNormal;
// Step 13: the surface tangent — the model-space direction of increasing texture U.
// Together with the normal it forms the basis that orients a tangent-space normal map.
layout(location = 4) in vec3 inTangent;

// --- Per-draw uniform (the same for every vertex this draw) ----------------
// A uniform buffer holds values constant across the whole draw call, unlike the
// per-vertex `in` attributes above. SDL3's GPU API places VERTEX-stage uniform
// buffers in descriptor set 1 (set 0 is for textures/storage); we upload this each
// frame with SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, ...). All mat4s are
// column-major, matching koi::Mat4. Step 7 added `model` (the world matrix) on its
// own: lighting happens in WORLD space, so we need the world position and world
// normal, which the combined `mvp` alone can't give us. Step 19 added
// `normalMatrix` = transpose(inverse(model)) so normals survive a non-uniform scale.
layout(set = 1, binding = 0) uniform UBO {
    mat4 mvp;           // proj * view * model — straight to clip space
    mat4 model;         // model (world) matrix — places this object in the world
    mat4 normalMatrix;  // transpose(inverse(model)) — the correct transform for normals
};

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;     // for the eye/specular direction
layout(location = 3) out vec3 vWorldNormal;  // for diffuse/specular dot products
layout(location = 4) out vec3 vWorldTangent; // for building the TBN (normal mapping)

void main() {
    // Promote the 3D position to homogeneous coordinates (w = 1, marking it a
    // point) and transform it into clip space. The GPU does the w-divide next.
    gl_Position = mvp * vec4(inPosition, 1.0);

    // World-space position of this vertex (drop the homogeneous w, which is 1).
    vWorldPos = vec3(model * vec4(inPosition, 1.0));

    // Carry the normal into world space with the NORMAL MATRIX, not `model`.
    // A normal is a covector (it must stay perpendicular to the surface), so under
    // a non-uniform scale — say 2x on X only — multiplying by model would tilt it
    // off the surface. transpose(inverse(model)) is the transform that keeps it
    // perpendicular; for a pure rotation/uniform scale it reduces to mat3(model)
    // up to a length the fragment shader's normalize() discards anyway.
    vWorldNormal = mat3(normalMatrix) * inNormal;

    // The TANGENT, unlike the normal, IS an ordinary surface direction, so it
    // rides the plain `model` matrix (mat3). The fragment shader then re-
    // orthonormalizes it against the interpolated normal and rebuilds the TBN
    // basis from the pair — so a tangent-space normal map can be lit in world space.
    vWorldTangent = mat3(model) * inTangent;

    vColor = inColor;
    vUV = inUV;  // pass the texture coordinate straight through to the fragment stage
}
