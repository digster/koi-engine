// ============================================================================
//  skybox.vert — vertex shader for the environment cubemap (Step 14)
// ----------------------------------------------------------------------------
//  The skybox is a unit cube drawn AROUND the camera. Its whole trick is that a
//  cube corner's OBJECT-SPACE position doubles as a DIRECTION: a cubemap is
//  sampled not by a 2D (u,v) but by a 3D vector pointing outward from the cube's
//  centre. So we simply forward the raw corner position to the fragment shader,
//  and after interpolation each fragment receives the exact view ray it needs to
//  look up the sky. (The cube sits at the origin of its own space, so its corner
//  positions ARE directions — no separate "sample direction" is needed.)
//
//  Two ideas make the sky sit correctly *behind everything*:
//   * TRANSLATION-STRIPPED VIEW. We feed in `skyViewProj = projection · view`,
//     but with the view matrix's TRANSLATION removed — only its rotation kept.
//     That keeps the sky "infinitely far": it turns as the camera turns yet never
//     slides as the camera moves, so you can never walk up to its walls.
//   * FAR-PLANE DEPTH. We force the output depth to the far plane by writing
//     `gl_Position = clip.xyww`. The GPU divides x/y/z by w after this shader;
//     setting z = w makes the post-divide depth w/w = 1.0 — the maximum. Paired
//     with a LEQUAL depth test and depth-writes OFF (configured on the pipeline),
//     the sky fills only pixels no geometry has already claimed, and hides nothing.
// ============================================================================
#version 450

// Only the position is read. The cube mesh also carries color/uv/normal/tangent,
// but — exactly like the shadow pass — the skybox pipeline declares just this one
// attribute at the full Vertex stride.
layout(location = 0) in vec3 inPosition;

// projection · (view with its translation zeroed), pushed once per frame via
// SDL_PushGPUVertexUniformData (VERTEX-stage uniform buffer → set 1, binding 0).
layout(set = 1, binding = 0) uniform SkyUBO {
    mat4 skyViewProj;
};

// The view direction handed to the fragment shader: the cube-local corner, which
// the rasterizer interpolates into a per-fragment ray (re-normalized on use).
layout(location = 0) out vec3 vDir;

void main() {
    vDir = inPosition;

    // Project the corner, then swizzle z into w (.xyww) so the depth after the
    // perspective divide pins to 1.0 — the far plane.
    vec4 clip = skyViewProj * vec4(inPosition, 1.0);
    gl_Position = clip.xyww;
}
