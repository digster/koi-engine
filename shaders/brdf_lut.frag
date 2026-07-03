// ============================================================================
//  brdf_lut.frag — the environment BRDF lookup table bake (Step 15)
// ----------------------------------------------------------------------------
//  The SECOND factor of the specular split-sum. Epic's insight was that the specular
//  IBL integral separates into (prefiltered environment) × (a small 2D function of
//  just the view angle and roughness) — and that second factor DOESN'T depend on the
//  environment at all. So we bake it once, ever, into a 2D texture indexed by
//  (N·V on x, roughness on y). Each texel holds a "scale" and "bias"; at runtime the
//  shader reconstructs the material's specular response as `F0 * scale + bias`.
//
//  Each texel is a Monte-Carlo integration over the GGX lobe (importance-sampled, with
//  the IBL geometry term), with Fresnel factored out so F0 can be applied later. This
//  runs as a single fullscreen pass (reusing fullscreen.vert) into an RG float target.
//  The maths mirrors renderer/Pbr.hpp (integrateBRDF); the CPU twin is unit-tested.
// ============================================================================
#version 450

// fullscreen.vert hands us the [0,1]² screen coordinate; we read it as (N·V, roughness).
layout(location = 0) in vec2 vUV;

// Two channels: the scale and bias of the split-sum. Target is an RG float texture.
layout(location = 0) out vec2 outColor;

const float PI = 3.14159265359;

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
// IBL geometry term: note k = roughness²/2, NOT the direct-lighting (r+1)²/8 remap.
float geometrySchlickGGX(float nDotX, float roughness) {
    float k = (roughness * roughness) / 2.0;
    return nDotX / (nDotX * (1.0 - k) + k);
}
float geometrySmith(float nDotV, float nDotL, float roughness) {
    return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

// The split-sum's environment BRDF for one (N·V, roughness) cell. Fix N = +Z, place V
// in the X–Z plane at the given N·V, importance-sample H, reflect to get L, accumulate
// the geometry+Fresnel weighted response with Fresnel's F0 factored out (Fc).
vec2 integrateBRDF(float nDotV, float roughness) {
    vec3 V = vec3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    vec3 N = vec3(0.0, 0.0, 1.0);

    float scale = 0.0;
    float bias  = 0.0;
    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 H  = importanceSampleGGX(xi, N, roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float nDotL = max(L.z, 0.0);
        float nDotH = max(H.z, 0.0);
        float vDotH = max(dot(V, H), 0.0);
        if (nDotL > 0.0) {
            float G    = geometrySmith(nDotV, nDotL, roughness);
            float gVis = (G * vDotH) / max(nDotH * nDotV, 1e-7);
            float Fc   = pow(1.0 - vDotH, 5.0);
            scale += (1.0 - Fc) * gVis;
            bias  += Fc * gVis;
        }
    }
    return vec2(scale, bias) / float(SAMPLE_COUNT);
}

void main() {
    // Guard the left edge (N·V = 0) against the degenerate head-on-at-grazing case.
    outColor = integrateBRDF(max(vUV.x, 1e-4), vUV.y);
}
