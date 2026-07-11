// ============================================================================
//  shadow.vert — vertex shader for the shadow (depth) pass
// ----------------------------------------------------------------------------
//  Shadow mapping renders the scene a FIRST time from the LIGHT's point of view,
//  recording only depth (how far the nearest surface is, as seen from the light)
//  into a "shadow map" texture. This shader does the bare minimum for that pass:
//  transform each vertex into the light's clip space. It reads only the position
//  (the meshes carry color/uv/normal too, but depth doesn't need them).
//
//  INSTANCED (Step 24). The per-object MODEL matrix is no longer pushed as a
//  uniform per draw; it arrives as a PER-INSTANCE vertex attribute (locations 1–4),
//  so many copies of a mesh draw in one call — each instance advancing to its own
//  model matrix. Only the light's view-projection stays a (shared) uniform.
// ============================================================================
#version 450

// Per-vertex: just the model-space position (depth needs nothing else).
layout(location = 0) in vec3 inPosition;

// Per-INSTANCE: this copy's model (world) matrix. A mat4 attribute occupies four
// consecutive locations (1,2,3,4), one per column; the pipeline binds it from a
// second vertex buffer at INSTANCE rate, so it advances once per instance.
layout(location = 1) in mat4 inModel;

// The light's view-projection, shared by every instance (set 1, binding 0), pushed
// once per shadow pass via SDL_PushGPUVertexUniformData.
layout(set = 1, binding = 0) uniform UBO {
    mat4 lightViewProj;
};

void main() {
    gl_Position = lightViewProj * inModel * vec4(inPosition, 1.0);
}
