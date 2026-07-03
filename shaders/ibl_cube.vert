// ============================================================================
//  ibl_cube.vert — vertex shader for the IBL cubemap bake passes (Step 15)
// ----------------------------------------------------------------------------
//  Both environment convolutions (diffuse irradiance and specular prefilter) are
//  computed by RENDERING INTO each of the six cubemap faces, one draw per face. To
//  do that we draw a unit cube from the origin with a 90°-FOV camera pointed down
//  the axis of the face being baked, so that face's square exactly fills the output.
//
//  The trick is identical to the skybox's: a cube corner's OBJECT-SPACE position is
//  also a DIRECTION out of the centre. Since the cube sits at the origin with no
//  model rotation, the interpolated position handed to the fragment shader IS the
//  world-space direction that texel represents — exactly the vector the fragment
//  shader must convolve the environment around. The per-face `captureViewProj` only
//  decides WHICH texels each cube face lands on; it doesn't change the direction.
//
//  Unlike skybox.vert we do NOT force the far plane (.xyww): these are off-screen
//  bake targets with no depth buffer, so a normal projected position is fine.
// ============================================================================
#version 450

// Position only — the bake pipelines declare a single vertex attribute at the full
// Vertex stride, the same way the shadow and skybox pipelines reuse the cube mesh.
layout(location = 0) in vec3 inPosition;

// The current face's view-projection: perspective(90°) * lookAt(origin -> face axis).
// Pushed per face via SDL_PushGPUVertexUniformData (vertex set 1, binding 0).
layout(set = 1, binding = 0) uniform CaptureUBO {
    mat4 captureViewProj;
};

// The world-space sample direction for this cube corner, interpolated per fragment.
layout(location = 0) out vec3 vDir;

void main() {
    vDir = inPosition;
    gl_Position = captureViewProj * vec4(inPosition, 1.0);
}
