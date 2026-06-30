// ============================================================================
//  shadow.frag — fragment shader for the shadow (depth) pass
// ----------------------------------------------------------------------------
//  The shadow pass renders into a depth-only target (NO color attachment), so the
//  fragment shader has nothing to output: the GPU's fixed-function depth test
//  writes each fragment's depth into the shadow map automatically. This shader is
//  deliberately empty — it exists only because the pipeline needs a fragment
//  stage. The depth we care about is produced for free by rasterization.
// ============================================================================
#version 450

void main() {
}
