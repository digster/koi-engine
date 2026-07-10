// ============================================================================
//  debug_line.frag — fragment shader for debug lines (Step 22)
// ----------------------------------------------------------------------------
//  Unlit and unconditional: output the interpolated colour as-is, fully opaque.
//  Debug lines carry no material, no lighting, no texture — the whole point is a
//  crisp, predictable colour you can read at a glance.
//
//  Note the lines are drawn INTO the HDR scene target (so they also appear in
//  KOI_CAPTURE frames), which means the post-processing tone-map will compress
//  their colour slightly and FXAA may soften them a touch. That's an accepted
//  trade for having debug output show up in the same image the scene does; pick
//  bright, saturated colours on the CPU side and they read fine.
// ============================================================================
#version 450

layout(location = 0) in vec3 vColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(vColor, 1.0);
}
