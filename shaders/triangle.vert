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
//
//  INSTANCED (Step 24). The per-object matrices used to be pushed as a uniform per
//  draw. Now they arrive as PER-INSTANCE vertex attributes, so many copies of a mesh
//  draw in ONE call — the GPU advances to the next instance's matrices once per copy
//  (not once per vertex). Only the camera's view-projection — the same for every
//  object this frame — stays a shared uniform, and we form the MVP here in-shader.
// ============================================================================
#version 450

// --- Per-vertex inputs (from the mesh vertex buffer, slot 0) ----------------
// inPosition is a vec3: the cube lives in real 3D space.
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

// --- Per-INSTANCE inputs (from the instance buffer, slot 1) -----------------
// Step 24: this copy's transforms. Each mat4 attribute occupies FOUR consecutive
// locations (one per column), bound from a second vertex buffer at INSTANCE rate so
// it advances once per instance rather than once per vertex. `inModel` places this
// copy in the world; `inNormalMatrix` = transpose(inverse(model)) keeps its normals
// perpendicular under a non-uniform scale (Step 19) — precomputed on the CPU per
// instance, since inverting a matrix in the shader would be wasteful.
layout(location = 5) in mat4 inModel;         // occupies locations 5,6,7,8
layout(location = 9) in mat4 inNormalMatrix;  // occupies locations 9,10,11,12

// --- Shared uniform (the same for every vertex AND every instance) ----------
// SDL3's GPU API places VERTEX-stage uniform buffers in descriptor set 1 (set 0 is
// for textures/storage); we push this once per pass with SDL_PushGPUVertexUniformData
// (slot 0). It is now just the camera's projection * view — the MVP is formed below
// as viewProj * inModel, so the per-object part comes from the instance attributes.
layout(set = 1, binding = 0) uniform UBO {
    mat4 viewProj;  // projection * view — the camera, shared by all instances
};

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;     // for the eye/specular direction
layout(location = 3) out vec3 vWorldNormal;  // for diffuse/specular dot products
layout(location = 4) out vec3 vWorldTangent; // for building the TBN (normal mapping)

void main() {
    // Form this instance's MVP from the shared camera matrix and its own model
    // matrix, then transform the model-space position to clip space (the GPU does
    // the w-divide next). Promote to homogeneous coords (w = 1, marking it a point).
    mat4 mvp = viewProj * inModel;
    gl_Position = mvp * vec4(inPosition, 1.0);

    // World-space position of this vertex (drop the homogeneous w, which is 1).
    vWorldPos = vec3(inModel * vec4(inPosition, 1.0));

    // Carry the normal into world space with the NORMAL MATRIX, not `model`.
    // A normal is a covector (it must stay perpendicular to the surface), so under
    // a non-uniform scale — say 2x on X only — multiplying by model would tilt it
    // off the surface. transpose(inverse(model)) is the transform that keeps it
    // perpendicular; for a pure rotation/uniform scale it reduces to mat3(model)
    // up to a length the fragment shader's normalize() discards anyway.
    vWorldNormal = mat3(inNormalMatrix) * inNormal;

    // The TANGENT, unlike the normal, IS an ordinary surface direction, so it
    // rides the plain `model` matrix (mat3). The fragment shader then re-
    // orthonormalizes it against the interpolated normal and rebuilds the TBN
    // basis from the pair — so a tangent-space normal map can be lit in world space.
    vWorldTangent = mat3(inModel) * inTangent;

    vColor = inColor;
    vUV = inUV;  // pass the texture coordinate straight through to the fragment stage
}
