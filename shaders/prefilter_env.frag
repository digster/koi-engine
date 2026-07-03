// ============================================================================
//  prefilter_env.frag — specular IBL prefilter bake pass (Step 15)
// ----------------------------------------------------------------------------
//  Produces the first half of the SPECULAR split-sum: a "prefiltered" environment
//  cubemap that stores the environment pre-blurred by roughness. A mirror reflects a
//  crisp copy of its surroundings; a rough metal reflects a smeared one. We bake that
//  blur ONCE, at several roughness levels, and store each level in a MIP of this cube:
//  mip 0 = roughness 0 (sharp), the last mip = roughness 1 (fully blurred). At runtime
//  triangle.frag samples this map along the reflection vector with textureLod(roughness),
//  and the hardware's trilinear filter interpolates between the two nearest roughness
//  levels for free.
//
//  The blur isn't a plain average — it's a GGX-weighted one: we IMPORTANCE-SAMPLE the
//  microfacet lobe (draw reflection directions in proportion to how the material's
//  roughness actually scatters light) and average what the environment shows along
//  them. To keep bright pixels from producing sparkly "fireflies" with a finite sample
//  count, each sample reads a MIP of the source env chosen from its probability — dense
//  samples read sharp mips, sparse ones read blurrier mips.
//
//  The maths (Hammersley / importanceSampleGGX / GGX D) mirrors renderer/Pbr.hpp.
// ============================================================================
#version 450

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

// Source environment (the loaded skybox cubemap), WITH its mip chain (fragment set 2,
// binding 0). We sample its mips to suppress fireflies — see the pdf-based mip below.
layout(set = 2, binding = 0) uniform samplerCube uEnv;

// Per-mip bake parameters (fragment set 3, binding 0):
//   x = roughness for this mip (0 at mip 0 .. 1 at the last mip)
//   y = source env face resolution in texels (for the firefly-suppression mip select)
layout(set = 3, binding = 0) uniform PrefilterUBO {
    vec4 params;
};

const float PI = 3.14159265359;

// Match skybox.frag's brightness lift so reflections agree with the visible sky.
const float kSkyIntensity = 1.25;

// --- Pure helpers, mirroring renderer/Pbr.hpp -------------------------------
float radicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;  // / 2^32
}
vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radicalInverseVdC(i));
}
vec3 importanceSampleGGX(vec2 xi, vec3 n, float roughness) {
    float a = roughness * roughness;
    float phi      = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3  h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3  up      = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3  tangent = normalize(cross(up, n));
    vec3  bitan   = cross(n, tangent);
    return normalize(tangent * h.x + bitan * h.y + n * h.z);
}
float distributionGGX(float nDotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

void main() {
    float roughness  = params.x;
    float resolution = params.y;

    // Simplifying assumption (Epic 2013): treat the view and reflection as aligned with
    // the normal. It drops the elongated grazing highlight but makes a single prefilter
    // map (independent of view) possible — the whole point of the split-sum.
    vec3 N = normalize(vDir);
    vec3 R = N;
    vec3 V = N;

    const uint SAMPLE_COUNT = 1024u;
    vec3  prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(xi, N, roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);  // reflect V about the sampled H

        float nDotL = max(dot(N, L), 0.0);
        if (nDotL > 0.0) {
            // Choose a source mip from the sample's solid angle vs. a texel's solid
            // angle: sparse (low-pdf) samples read a blurrier mip, which averages away
            // the bright specks that would otherwise sparkle.
            float nDotH   = max(dot(N, H), 0.0);
            float hDotV   = max(dot(H, V), 0.0);
            float D       = distributionGGX(nDotH, roughness);
            float pdf     = (D * nDotH / (4.0 * hDotV)) + 0.0001;
            float saTexel  = 4.0 * PI / (6.0 * resolution * resolution);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);
            float mip      = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel);

            prefiltered += textureLod(uEnv, L, mip).rgb * kSkyIntensity * nDotL;
            totalWeight += nDotL;
        }
    }
    prefiltered = prefiltered / max(totalWeight, 0.001);
    outColor = vec4(prefiltered, 1.0);
}
