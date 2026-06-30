// ============================================================================
//  shadow.vert — vertex shader for the shadow (depth) pass
// ----------------------------------------------------------------------------
//  Shadow mapping renders the scene a FIRST time from the LIGHT's point of view,
//  recording only depth (how far the nearest surface is, as seen from the light)
//  into a "shadow map" texture. This shader does the bare minimum for that pass:
//  transform each vertex by the light's view-projection × the object's model
//  matrix, straight to the light's clip space. It reads only the position (the
//  meshes carry color/uv/normal too, but depth doesn't need them).
// ============================================================================
#version 450

layout(location = 0) in vec3 inPosition;

// The combined lightViewProj * model, pushed per object via
// SDL_PushGPUVertexUniformData (vertex uniform buffer, set 1).
layout(set = 1, binding = 0) uniform UBO {
    mat4 lightMvp;
};

void main() {
    gl_Position = lightMvp * vec4(inPosition, 1.0);
}
