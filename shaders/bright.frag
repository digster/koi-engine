// ============================================================================
//  bright.frag — bloom bright-pass / threshold (Step 10)
// ----------------------------------------------------------------------------
//  BLOOM is the soft glow real cameras and eyes show around very bright things
//  (a lamp, sunlight on chrome). We fake it in three stages:
//    1. EXTRACT the bright parts of the image  <-- this shader
//    2. BLUR them so they bleed outward         (blur.frag)
//    3. ADD that blurred glow back on top       (composite.frag)
//
//  This first stage keeps only the pixels brighter than a threshold and discards
//  the rest (outputs black). Because the scene was rendered into an HDR target,
//  "bright" can mean genuinely > 1.0 (a hot specular highlight) — which is exactly
//  what should glow. We run this into a HALF-resolution target: bloom is blurry by
//  definition, so the lost detail is invisible and the blur becomes much cheaper.
// ============================================================================
#version 450

layout(location = 0) in vec2 vUV;

// The HDR scene texture (fragment sampler set = 2, binding 0).
layout(set = 2, binding = 0) uniform sampler2D uScene;

// Bloom parameters (fragment uniform set = 3, binding 0): x = brightness threshold.
layout(set = 3, binding = 0) uniform BrightUBO {
    vec4 params;
};

layout(location = 0) out vec4 outColor;

// Perceived brightness of a colour (Rec. 709 luma weights — green dominates because
// our eyes are most sensitive to it).
float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    float threshold = params.x;

    // Soft knee: instead of a hard "in or out" cutoff (which makes the glow's edge
    // pop on and off as things cross the threshold), keep the FRACTION of the colour
    // that lies above the threshold. A pixel just over the line contributes faintly;
    // a very bright one contributes nearly all of itself.
    float lum = luminance(hdr);
    float contribution = max(lum - threshold, 0.0) / max(lum, 1e-5);

    outColor = vec4(hdr * contribution, 1.0);
}
