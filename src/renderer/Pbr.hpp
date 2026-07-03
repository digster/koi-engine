// ============================================================================
//  Pbr.hpp — pure Cook-Torrance BRDF helpers (Step 12)
// ----------------------------------------------------------------------------
//  Step 12 replaces the ad-hoc Blinn-Phong shading with a physically-based
//  **Cook-Torrance** BRDF (the metallic-roughness model). The lighting itself runs
//  on the GPU in triangle.frag, but the three microfacet sub-terms it multiplies
//  together are small, pure functions worth pinning down. This header holds their
//  CPU mirrors, exactly like PostProcess.hpp mirrors the tone-map curve and
//  Light.hpp mirrors the attenuation curve:
//
//    * D — the NORMAL DISTRIBUTION (GGX/Trowbridge-Reitz): what fraction of the
//      surface's microscopic facets happen to point along the halfway vector H, and
//      so reflect the light straight at the eye. Roughness controls how spread-out
//      this lobe is (smooth = a tight, bright highlight).
//    * G — the GEOMETRY term (Smith with Schlick-GGX): microfacets shadow and mask
//      each other at grazing angles, so not all of D's facets are actually visible.
//    * F — FRESNEL (Schlick): every surface reflects MORE at grazing angles. F0 is
//      the reflectance when looking straight on (≈0.04 for non-metals; the albedo
//      for metals).
//
//  The specular BRDF is (D·G·F) / (4·(N·V)·(N·L)). These helpers are the shader's
//  CPU twin — the shader stays the runtime source of truth; these exist so the math
//  can be unit-tested headlessly (tests/test_pbr.cpp) and documented in one place.
//
//  Deliberately SDL-free (plain <cmath>/<algorithm>) so it's cheap to include in the
//  tests without pulling in the GPU headers.
//
//  Step 15 adds a second family of helpers below the direct-lighting D/G/F: the pure
//  functions the IMAGE-BASED LIGHTING bake runs. IBL integrates the environment (the
//  skybox) over the hemisphere so surfaces are lit by their surroundings — the fix for
//  the flat ambient that makes metals look dark. The heavy integrals are precomputed on
//  the GPU (irradiance_convolution.frag / prefilter_env.frag / brdf_lut.frag), but their
//  building blocks — the Hammersley low-discrepancy sequence, GGX importance sampling,
//  the IBL geometry term, and the split-sum BRDF integration — are small pure functions.
//  They live here as the shaders' CPU twin so the math is documented once and unit-tested
//  (tests/test_ibl.cpp) without a GPU.
// ============================================================================
#pragma once

#include <algorithm>  // std::clamp, std::max
#include <cmath>      // std::pow, std::sqrt, std::sin, std::cos
#include <cstdint>    // std::uint32_t (Hammersley bit-reversal)

#include "math/Vec.hpp"  // Vec2/Vec3 returns (SDL-free)

namespace koi {

// π, to the precision a float can hold. The Lambertian diffuse term divides by this
// (energy conservation), and the GGX distribution has it in its denominator.
inline constexpr float kPi = 3.14159265358979323846f;

// D — GGX / Trowbridge-Reitz normal distribution. `nDotH` is how aligned the surface
// normal is with the halfway vector; `roughness` in [0,1]. We square roughness into
// the microfacet parameter `a` (the common "roughness²" remap) so the dial feels
// perceptually linear. Peaks at nDotH = 1 (a mirror facet), and a rougher surface
// gives a lower, wider peak — the visual meaning of roughness.
[[nodiscard]] inline float distributionGGX(float nDotH, float roughness) {
    const float a      = roughness * roughness;
    const float a2     = a * a;
    const float nDotH2 = nDotH * nDotH;
    float       denom  = nDotH2 * (a2 - 1.0f) + 1.0f;
    denom = kPi * denom * denom;
    return a2 / std::max(denom, 1e-7f);
}

// One half of the Smith geometry term (Schlick-GGX approximation) for a single
// direction. `nDotX` is N·V or N·L. For DIRECT lighting the roughness is remapped to
// k = (roughness+1)² / 8. Returns a visibility factor in [0,1] (1 when facing the
// direction head-on, 0 at the grazing limit).
[[nodiscard]] inline float geometrySchlickGGX(float nDotX, float roughness) {
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    return nDotX / (nDotX * (1.0f - k) + k);
}

// G — Smith's method: apply the Schlick-GGX term for BOTH the view and light
// directions (geometry occlusion happens on the way in AND the way out) and multiply.
// In [0,1]; shrinks as roughness grows (rough surfaces self-shadow more).
[[nodiscard]] inline float geometrySmith(float nDotV, float nDotL, float roughness) {
    return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

// F — Fresnel-Schlick: reflectance rises from F0 (head-on) toward 1.0 at grazing
// angles. `cosTheta` is typically H·V. Scalar form here; the shader runs the exact
// same formula per RGB channel (F0 is a vec3 there — 0.04 for dielectrics, the
// albedo for metals).
[[nodiscard]] inline float fresnelSchlick(float cosTheta, float f0) {
    const float m = std::clamp(1.0f - cosTheta, 0.0f, 1.0f);
    return f0 + (1.0f - f0) * (m * m * m * m * m);  // (1 - cosTheta)^5
}

// --- Image-based lighting (IBL) precompute helpers (Step 15) ------------------
//
//  These mirror the bake shaders. The two specular-IBL integrals (prefiltering the
//  environment and building the BRDF LUT) have no closed form, so we estimate them by
//  MONTE CARLO: average many samples of the integrand. Uniform random samples would be
//  noisy, so we use two standard tricks — a LOW-DISCREPANCY sequence (Hammersley) for
//  evenly-spread sample points, and IMPORTANCE SAMPLING (draw half-vectors from the GGX
//  lobe itself) so samples land where the BRDF actually has weight.

// Van der Corput radical inverse: mirror the bits of `i` about the binary point, giving
// a value in [0,1) that fills the interval as evenly as possible as i increases. This is
// the second dimension of the Hammersley sequence. Branch-free bit reversal (the exact
// same sequence the GLSL bake computes, so CPU and GPU agree sample-for-sample).
[[nodiscard]] inline float radicalInverseVdC(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;  // / 2^32
}

// The i-th of N Hammersley points on the unit square: (i/N, radicalInverseVdC(i)). A
// deterministic, well-distributed stand-in for a pair of uniform random numbers.
[[nodiscard]] inline Vec2 hammersley(std::uint32_t i, std::uint32_t n) {
    return {static_cast<float>(i) / static_cast<float>(n), radicalInverseVdC(i)};
}

// Importance-sample the GGX distribution: map a Hammersley point `xi` to a microfacet
// half-vector H drawn in proportion to the NDF around the surface normal `N`. Concentrates
// samples in the specular lobe (tight for low roughness, wide for high), which is what
// makes the Monte-Carlo estimate converge with a feasible sample count. Returns a unit
// world-space vector. `roughness` is the perceptual roughness (squared into `a` as usual).
[[nodiscard]] inline Vec3 importanceSampleGGX(Vec2 xi, Vec3 n, float roughness) {
    const float a = roughness * roughness;

    // Spherical angles of H in the tangent frame: phi spun uniformly, cosTheta drawn from
    // the GGX cumulative distribution so denser samples sit near the lobe's axis.
    const float phi      = 2.0f * kPi * xi.x;
    const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

    // Tangent-space half-vector, then rotate into world space with a basis built around N.
    const Vec3 hTan{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta};
    const Vec3 up = (std::fabs(n.z) < 0.999f) ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 tangent   = normalize(cross(up, n));
    const Vec3 bitangent = cross(n, tangent);
    return normalize(tangent * hTan.x + bitangent * hTan.y + n * hTan.z);
}

// Smith geometry term for IBL. CRUCIAL DIFFERENCE from the direct-lighting version in
// geometrySchlickGGX above: IBL remaps roughness with k = roughness²/2, NOT (r+1)²/8.
// Using the direct-light remap here is a classic, silent IBL bug (over-darkened edges), so
// the two live as separate functions on purpose.
[[nodiscard]] inline float geometrySchlickGGXIBL(float nDotX, float roughness) {
    const float k = (roughness * roughness) / 2.0f;
    return nDotX / (nDotX * (1.0f - k) + k);
}
[[nodiscard]] inline float geometrySmithIBL(float nDotV, float nDotL, float roughness) {
    return geometrySchlickGGXIBL(nDotV, roughness) * geometrySchlickGGXIBL(nDotL, roughness);
}

// Roughness-aware Fresnel (Sébastien Lagarde): like fresnelSchlick, but the grazing
// reflectance is capped by (1 - roughness) so rough surfaces don't get an unrealistically
// bright rim from the environment. Used for the AMBIENT (IBL) term, where there's no single
// light direction to take H·V from — the view angle nDotV stands in. Scalar CPU twin; the
// shader runs it per RGB channel with a vec3 F0.
[[nodiscard]] inline float fresnelSchlickRoughness(float cosTheta, float f0, float roughness) {
    const float m       = std::clamp(1.0f - cosTheta, 0.0f, 1.0f);
    const float fMax    = std::max(1.0f - roughness, f0);  // the vec3 max(1-rough, F0) per channel
    return f0 + (fMax - f0) * (m * m * m * m * m);
}

// Integrate the environment BRDF for one (nDotV, roughness) cell of the BRDF LUT — the
// second half of the SPLIT-SUM approximation. The result is a scale + bias, INDEPENDENT of
// the actual environment: at runtime the shader reads it as `F0 * lut.x + lut.y` to weight
// the prefiltered reflection. We fix N = +Z and place V in the X–Z plane at the given
// nDotV, importance-sample H, reflect V about H to get L, and accumulate the geometry- and
// Fresnel-weighted contribution. `samples` defaults to the shader's 1024.
[[nodiscard]] inline Vec2 integrateBRDF(float nDotV, float roughness,
                                        std::uint32_t samples = 1024u) {
    nDotV = std::clamp(nDotV, 1e-4f, 1.0f);
    const Vec3 v{std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV};  // sin, 0, cos
    const Vec3 n{0.0f, 0.0f, 1.0f};

    float scale = 0.0f;  // weight applied to F0
    float bias  = 0.0f;  // constant added on top
    for (std::uint32_t i = 0; i < samples; ++i) {
        const Vec2  xi = hammersley(i, samples);
        const Vec3  h  = importanceSampleGGX(xi, n, roughness);
        const Vec3  l  = h * (2.0f * dot(v, h)) - v;  // reflect V about H

        const float nDotL = std::max(l.z, 0.0f);
        const float nDotH = std::max(h.z, 0.0f);
        const float vDotH = std::max(dot(v, h), 0.0f);
        if (nDotL > 0.0f) {
            const float g    = geometrySmithIBL(nDotV, nDotL, roughness);
            const float gVis = (g * vDotH) / std::max(nDotH * nDotV, 1e-7f);
            const float fc   = std::pow(1.0f - vDotH, 5.0f);  // Fresnel, factored out of F0
            scale += (1.0f - fc) * gVis;
            bias  += fc * gVis;
        }
    }
    return {scale / static_cast<float>(samples), bias / static_cast<float>(samples)};
}

}  // namespace koi
