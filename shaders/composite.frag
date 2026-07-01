// ============================================================================
//  composite.frag — combine + tone-map + vignette + gamma (Step 10)
// ----------------------------------------------------------------------------
//  This is the heart of post-processing: it takes the HDR scene (and the blurred
//  bloom) and produces the final displayable colour. Four ideas converge here:
//
//  * EXPOSURE — a single multiplier, like a camera's exposure dial: scale the whole
//    HDR image up/down before tone-mapping to brighten or darken the result.
//
//  * TONE-MAPPING — the scene is HDR: its brightest pixels can be far above 1.0, but
//    a monitor can only show [0,1]. Naively clamping blows highlights to flat white.
//    A tone-map curve instead gently COMPRESSES the high range so detail survives in
//    bright areas (and shadows keep contrast). We use the ACES filmic approximation,
//    the same family of curve used in film and modern games.
//
//  * VIGNETTE — a subtle darkening toward the corners, a classic lens/photographic
//    look that draws the eye to the centre.
//
//  * GAMMA (linear -> sRGB) — we light and blend in LINEAR colour (where 0.5 means
//    "half the light"), but displays are sRGB: they expect a non-linear encoding
//    where ~0.73 maps to half-brightness. Without this final encode the whole image
//    looks too dark. (Our input textures are treated as linear for simplicity — a
//    documented shortcut; a fuller pipeline would sRGB-DECODE them on the way in.)
// ============================================================================
#version 450

layout(location = 0) in vec2 vUV;

// Fragment samplers: the HDR scene (slot 0) and the blurred bloom (slot 1).
layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(set = 2, binding = 1) uniform sampler2D uBloom;

// Post parameters (set = 3, binding 0):
//   x = exposure, y = bloom intensity, z = vignette strength, w = tone-map enabled.
layout(set = 3, binding = 0) uniform CompositeUBO {
    vec4 params;
};

layout(location = 0) out vec4 outColor;

// ACES filmic tone-map (Narkowicz 2015 curve fit): maps unbounded HDR into ~[0,1]
// with a pleasing shoulder on the highlights and toe on the shadows.
vec3 acesToneMap(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    float exposure        = params.x;
    float bloomIntensity  = params.y;
    float vignetteStrength = params.z;
    bool  tonemap         = params.w > 0.5;

    vec3 hdr   = texture(uScene, vUV).rgb;
    vec3 bloom = texture(uBloom, vUV).rgb;

    // Add the glow on top of the scene (additive light), then apply exposure.
    vec3 color = (hdr + bloom * bloomIntensity) * exposure;

    // HDR -> LDR. With tone-mapping OFF we simply clamp, so you can directly SEE what
    // the curve buys you (toggle it at runtime): clamping flattens bright areas to
    // white, the curve keeps their shape.
    color = tonemap ? acesToneMap(color) : clamp(color, 0.0, 1.0);

    // Vignette: darken with the squared distance from the screen centre. Strength 0
    // disables it (the runtime toggle just sets this to 0).
    vec2 d = vUV - 0.5;
    float vignette = 1.0 - vignetteStrength * dot(d, d) * 2.0;
    color *= clamp(vignette, 0.0, 1.0);

    // Encode linear -> sRGB (approximate gamma 2.2) for the display.
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, 1.0);
}
