// ============================================================================
//  skybox.frag — fragment shader for the environment cubemap (Step 14)
// ----------------------------------------------------------------------------
//  Runs once per background pixel and returns the sky colour along that pixel's
//  view ray. A CUBEMAP (`samplerCube`) is six square textures arranged as the
//  faces of a cube; you sample it with a 3D DIRECTION vector instead of a 2D
//  coordinate, and the hardware works out which face that ray pierces and where
//  on it to read. So the whole shader is: normalize the interpolated ray, look it
//  up, and write the result.
//
//  Why the brightness lift? The scene renders into an HDR float target that the
//  post-processing chain later TONE-MAPS (exposure + a filmic curve) down to the
//  display. An 8-bit cubemap's colours live in [0,1], which would come out dim
//  after tone-mapping — so a small constant (kSkyIntensity) lifts the sky to a
//  believable brightness relative to the lit geometry. As a bonus, its brightest
//  texels (the sun) exceed the bloom threshold and bleed a soft glow for free via
//  the Step 10 bright-pass. (Tuned by eye through KOI_CAPTURE.)
//
//  This same cubemap is the groundwork for Step 15's image-based lighting, where
//  it stops being just a backdrop and starts actually LIGHTING the surfaces.
// ============================================================================
#version 450

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

// The environment cubemap (FRAGMENT-stage sampler → set 2, binding 0).
layout(set = 2, binding = 0) uniform samplerCube uSky;

// Scene-relative brightness of the sky in the HDR buffer, before tone-mapping.
// Tuned with the bloom threshold (0.9) in mind: the sky body stays under it while
// the sun disk (near 1.0 in the cubemap) lands above it and blooms. See gen_skybox.py.
const float kSkyIntensity = 1.25;

void main() {
    // The interpolated ray is generally not unit-length; normalize before the
    // cubemap lookup so the sampled face/texel is chosen from a true direction.
    vec3 dir = normalize(vDir);
    outColor = vec4(texture(uSky, dir).rgb * kSkyIntensity, 1.0);
}
