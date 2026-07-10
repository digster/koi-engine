// ============================================================================
//  hud.frag — fragment shader for the HUD overlay (Step 23)
// ----------------------------------------------------------------------------
//  Every HUD quad — a glyph or a filled panel — samples the same font ATLAS. The
//  atlas stores glyph COVERAGE: white and fully opaque where there is ink,
//  transparent where there isn't (and the reserved white cell is opaque
//  everywhere, which is what filled rects sample). So the shader is just:
//  read the atlas, multiply by the per-vertex tint.
//
//  Multiplying by the tint does two useful things at once: the RGB picks up the
//  text/panel COLOUR, and the alpha picks up both the glyph's coverage AND the
//  caller's requested opacity (a translucent panel uses colour.a < 1). The
//  pipeline's straight alpha blend then composites the result over the already
//  post-processed scene — which is why the HUD stays crisp: it's drawn AFTER
//  tone-mapping and FXAA, straight onto the final image.
// ============================================================================
#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

// The font atlas, bound at fragment set 2, binding 0 (SDL GPU's convention).
layout(set = 2, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texel = texture(uAtlas, vUV);  // (1,1,1,1) on ink / white cell, 0 elsewhere
    outColor = texel * vColor;          // tint colour + coverage/opacity in .a
}
