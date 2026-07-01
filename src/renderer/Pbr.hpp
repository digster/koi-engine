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
// ============================================================================
#pragma once

#include <algorithm>  // std::clamp
#include <cmath>      // std::pow

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

}  // namespace koi
