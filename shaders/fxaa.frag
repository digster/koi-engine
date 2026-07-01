// ============================================================================
//  fxaa.frag — Fast Approximate Anti-Aliasing (Step 10, final pass)
// ----------------------------------------------------------------------------
//  ALIASING is the "jaggies" — the stair-stepping along edges that happens because
//  each pixel is a single point sample: a slanted edge has to snap to the pixel grid.
//  True anti-aliasing (MSAA) renders extra samples per pixel, which costs memory and
//  bandwidth. FXAA (Timothy Lottes, NVIDIA) instead works as a cheap POST step on the
//  finished image: it finds edges by looking at local brightness (luma) contrast and
//  smooths just those, blending colour ALONG the edge. It's approximate — it can't
//  recover detail that was never sampled — but it's nearly free and softens jaggies
//  convincingly.
//
//  Important: FXAA must run on the FINAL, gamma-encoded LDR image (after tone-mapping),
//  because it reasons about PERCEIVED brightness — the non-linear values your eye and
//  the display actually see. That's why this is the very last pass.
//
//  This is the well-known compact FXAA (the "FXAA 3.11"-derived web version).
// ============================================================================
#version 450

layout(location = 0) in vec2 vUV;

// The tone-mapped LDR image to anti-alias (set = 2, binding 0).
layout(set = 2, binding = 0) uniform sampler2D uImage;

// Parameters (set = 3, binding 0): xy = 1/resolution (one texel in UV), z = enabled.
layout(set = 3, binding = 0) uniform FxaaUBO {
    vec4 params;
};

layout(location = 0) out vec4 outColor;

// FXAA tuning constants.
const float kEdgeThresholdMin = 0.0312;  // ignore edges below this absolute contrast
const float kEdgeThreshold    = 0.125;   // ... or below this fraction of the max luma
const float kSpanMax          = 8.0;     // clamp how far we search along the edge
const float kReduceMul        = 0.125;
const float kReduceMin        = 1.0 / 128.0;

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 inv = params.xy;       // size of one texel in UV space
    bool enabled = params.z > 0.5;

    vec3 rgbM = texture(uImage, vUV).rgb;  // this pixel
    if (!enabled) {                        // toggle off -> straight copy
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // Sample the four diagonal neighbours and compute everyone's luma.
    vec3 rgbNW = texture(uImage, vUV + vec2(-1.0, -1.0) * inv).rgb;
    vec3 rgbNE = texture(uImage, vUV + vec2( 1.0, -1.0) * inv).rgb;
    vec3 rgbSW = texture(uImage, vUV + vec2(-1.0,  1.0) * inv).rgb;
    vec3 rgbSE = texture(uImage, vUV + vec2( 1.0,  1.0) * inv).rgb;
    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);
    float lumaM  = luma(rgbM);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Flat region (no real edge): leave the pixel untouched. This keeps FXAA from
    // softening surfaces and texture detail — it only touches high-contrast edges.
    float range = lumaMax - lumaMin;
    if (range < max(kEdgeThresholdMin, lumaMax * kEdgeThreshold)) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // Estimate the edge's direction from the diagonal luma gradients, then walk
    // perpendicular to it, sampling a few points and averaging — that lateral blur
    // is what smooths the staircase.
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * kReduceMul, kReduceMin);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -kSpanMax, kSpanMax) * inv;

    // Two pairs of taps: a narrow average (rgbA) and a wider one that also includes
    // the edge extremes (rgbB). If the wide average strays outside the local luma
    // range we overshot, so fall back to the safer narrow average.
    vec3 rgbA = 0.5 * (
        texture(uImage, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(uImage, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(uImage, vUV + dir * -0.5).rgb +
        texture(uImage, vUV + dir *  0.5).rgb);

    float lumaB = luma(rgbB);
    outColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
