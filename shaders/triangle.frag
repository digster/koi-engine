// ============================================================================
//  triangle.frag — fragment shader for Step 1's "first triangle"
// ----------------------------------------------------------------------------
//  A fragment shader runs ONCE PER PIXEL that the triangle covers (a "fragment"
//  is a candidate pixel). Its job is to output that pixel's final color.
//
//  We simply pass through the incoming color. The magic is that vColor is NOT
//  the same value the vertex shader wrote — the rasterizer interpolated it from
//  the three corners, so a pixel in the middle of the triangle receives a blend
//  of red, green, and blue. That's the gradient.
// ============================================================================
#version 450

// Input from the vertex shader. The "location = 0" here must match the vertex
// shader's "layout(location = 0) out vec3 vColor".
layout(location = 0) in vec3 vColor;

// The pixel color we output. location = 0 means "the first (and only) color
// target" — i.e. the swapchain image we're drawing into.
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vColor, 1.0);   // rgb from the interpolated color, alpha = 1
}
