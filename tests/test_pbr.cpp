// ============================================================================
//  test_pbr.cpp — tests for the pure Cook-Torrance BRDF helpers (Step 12)
// ----------------------------------------------------------------------------
//  The PBR shading runs on the GPU, but its three microfacet sub-terms are pure
//  functions whose SHAPE we can pin down headlessly: Fresnel (grows to 1 at grazing
//  angles), the GGX distribution (a lobe that peaks at the mirror direction and
//  widens with roughness), and the Smith geometry term (a [0,1] visibility factor
//  that shrinks as roughness grows). These mirror the shader math in triangle.frag.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include <cmath>  // std::isfinite

#include "renderer/Pbr.hpp"

using koi::distributionGGX;
using koi::fresnelSchlick;
using koi::geometrySmith;

TEST_CASE("fresnelSchlick equals F0 head-on, reaches 1 at grazing, and rises between") {
    const float f0 = 0.04f;  // a typical dielectric

    // Looking straight down the surface (cosTheta = 1): reflectance is exactly F0.
    CHECK(fresnelSchlick(1.0f, f0) == doctest::Approx(f0));
    // At the grazing limit (cosTheta = 0): every surface becomes a mirror (→ 1).
    CHECK(fresnelSchlick(0.0f, f0) == doctest::Approx(1.0f));

    // Monotonic: as the view angle widens (cosTheta falls from 1 to 0), reflectance
    // only ever increases.
    float prev = fresnelSchlick(1.0f, f0);
    for (float c = 0.95f; c >= 0.0f; c -= 0.05f) {
        const float cur = fresnelSchlick(c, f0);
        CHECK(cur >= prev);
        prev = cur;
    }
}

TEST_CASE("distributionGGX peaks at the mirror direction and widens with roughness") {
    // For a fixed roughness the lobe is maximal when the microfacet faces H (nDotH=1).
    const float rough = 0.4f;
    const float peak  = distributionGGX(1.0f, rough);
    CHECK(peak > distributionGGX(0.8f, rough));
    CHECK(peak > distributionGGX(0.5f, rough));

    // Always positive and finite (even for a nearly-smooth surface).
    CHECK(distributionGGX(1.0f, 0.05f) > 0.0f);
    CHECK(std::isfinite(distributionGGX(1.0f, 0.05f)));

    // Rougher = a lower peak (energy spread over a wider lobe): the head-on value at
    // nDotH=1 falls as roughness climbs.
    CHECK(distributionGGX(1.0f, 0.2f) > distributionGGX(1.0f, 0.5f));
    CHECK(distributionGGX(1.0f, 0.5f) > distributionGGX(1.0f, 0.9f));
}

TEST_CASE("geometrySmith stays in [0,1] and shrinks as roughness grows") {
    const float nDotV = 0.7f;
    const float nDotL = 0.6f;

    float prev = geometrySmith(nDotV, nDotL, 0.05f);
    CHECK(prev <= 1.0f);
    CHECK(prev >= 0.0f);
    for (float r = 0.1f; r <= 1.0f; r += 0.1f) {
        const float cur = geometrySmith(nDotV, nDotL, r);
        CHECK(cur >= 0.0f);
        CHECK(cur <= 1.0f);
        CHECK(cur <= prev);  // more roughness → more self-shadowing → smaller G
        prev = cur;
    }
}
