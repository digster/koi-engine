// ============================================================================
//  triangle.frag — fragment shader (Step 6: samples a texture)
// ----------------------------------------------------------------------------
//  A fragment shader runs ONCE PER PIXEL that a triangle covers (a "fragment" is
//  a candidate pixel). Its job is to output that pixel's final color.
//
//  Since Step 6 that color comes from a TEXTURE: we look up the image at this
//  fragment's interpolated UV coordinate and multiply by the interpolated vertex
//  color (a per-vertex tint; a white tint shows the texture unchanged). Both the
//  UV and the color were interpolated across the triangle by the rasterizer.
// ============================================================================
#version 450

// Inputs from the vertex shader. Each "location" must match a vertex-shader "out".
layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;

// The texture to sample, as a combined image+sampler. SDL3's GPU API places
// FRAGMENT-stage sampled textures in descriptor SET 2 (the vertex stage uses
// sets 0/1); we bind the texture+sampler pair at slot 0 each frame with
// SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/0, ...).
layout(set = 2, binding = 0) uniform sampler2D uTex;

// The pixel color we output. location = 0 means "the first (and only) color
// target" — i.e. the swapchain image we're drawing into.
layout(location = 0) out vec4 outColor;

void main() {
    // Sample the texture at this fragment's UV, then tint by the vertex color.
    outColor = texture(uTex, vUV) * vec4(vColor, 1.0);
}
