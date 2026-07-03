// ============================================================================
//  irradiance_convolution.frag — diffuse IBL bake pass (Step 15)
// ----------------------------------------------------------------------------
//  Produces the DIFFUSE half of image-based lighting: an "irradiance" cubemap. Each
//  texel answers the question "if a matte surface faced this direction, how much
//  total light would it gather from the whole environment?" — the hemisphere of sky
//  above that surface, weighted by cos(theta) (light arriving edge-on contributes
//  less, exactly Lambert's cosine law).
//
//  There's no closed form, so we integrate NUMERICALLY: walk a grid of directions
//  over the hemisphere around the surface normal, sample the environment along each,
//  and accumulate cos·sin-weighted contributions (the sin comes from the shrinking
//  area of each ring of samples near the pole). The result is very low-frequency
//  (blurry), which is why a tiny 32² cubemap is plenty. At runtime triangle.frag
//  reads one texel of this map by the surface normal for the diffuse ambient.
//
//  This bake runs once at load, into each of the six faces via ibl_cube.vert.
// ============================================================================
#version 450

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

// The source environment: the loaded skybox cubemap (fragment set 2, binding 0).
layout(set = 2, binding = 0) uniform samplerCube uEnv;

const float PI = 3.14159265359;

// Match skybox.frag's brightness lift so the light the surfaces receive agrees with
// the sky you see. (The 8-bit sky lives in [0,1]; this scales it for the HDR buffer.)
const float kSkyIntensity = 1.25;

void main() {
    // The normal this irradiance texel represents.
    vec3 N = normalize(vDir);

    // Build a tangent frame around N so we can walk the hemisphere in its local space.
    vec3 up    = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));

    vec3  irradiance = vec3(0.0);
    float nrSamples  = 0.0;

    // Regular grid over the hemisphere: azimuth phi ∈ [0,2π), polar theta ∈ [0,π/2).
    const float sampleDelta = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            // Direction in tangent space, then rotated into world space by (right,up,N).
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            // cos(theta): Lambert's law. sin(theta): the differential solid-angle weight
            // (rings near the horizon cover more directions than rings near the pole).
            irradiance += texture(uEnv, sampleVec).rgb * kSkyIntensity * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }
    // The π and 1/N fold the cosine-weighted hemisphere integral into an average.
    irradiance = PI * irradiance * (1.0 / nrSamples);
    outColor = vec4(irradiance, 1.0);
}
