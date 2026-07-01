// ============================================================================
//  blur.frag — separable Gaussian blur (Step 10, bloom stage 2)
// ----------------------------------------------------------------------------
//  A Gaussian blur replaces each pixel with a weighted average of its neighbours,
//  the weights falling off in a bell curve — that's what turns the extracted bright
//  spots into a soft glow. A true 2D blur would average an N×N box of taps (e.g.
//  9×9 = 81 texture reads per pixel). Instead we exploit that a 2D Gaussian is
//  SEPARABLE: blurring horizontally and then vertically gives the same result with
//  only N+N taps. So this one shader runs twice — once with a horizontal step, once
//  with a vertical step — selected by the `direction` uniform.
//
//  Running this several times (ping-ponging between two textures) widens the glow.
// ============================================================================
#version 450

layout(location = 0) in vec2 vUV;

// The image to blur (the bright-pass output, or the previous blur result).
layout(set = 2, binding = 0) uniform sampler2D uImage;

// Blur parameters (set = 3, binding 0): xy = one tap's UV step along the blur axis
// (i.e. texelSize.x,0 for horizontal or 0,texelSize.y for vertical).
layout(set = 3, binding = 0) uniform BlurUBO {
    vec4 params;
};

layout(location = 0) out vec4 outColor;

void main() {
    // 5 distinct weights -> a 9-tap kernel (centre + 4 on each side). These are a
    // normalized Gaussian (they sum to 1 across all taps), so the blur preserves
    // overall brightness rather than darkening or brightening the image.
    const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 step = params.xy;

    vec3 result = texture(uImage, vUV).rgb * weight[0];
    for (int i = 1; i < 5; ++i) {
        result += texture(uImage, vUV + step * float(i)).rgb * weight[i];
        result += texture(uImage, vUV - step * float(i)).rgb * weight[i];
    }

    outColor = vec4(result, 1.0);
}
