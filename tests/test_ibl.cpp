// ============================================================================
//  test_ibl.cpp — tests for the pure image-based-lighting helpers (Step 15)
// ----------------------------------------------------------------------------
//  IBL's specular integrals are baked on the GPU, but their building blocks are pure
//  functions whose PROPERTIES we can pin down headlessly: the Hammersley sequence
//  (evenly spread points in the unit square), GGX importance sampling (unit vectors in
//  the upper hemisphere, collapsing onto the normal as roughness → 0), the split-sum
//  BRDF integration (a bounded scale/bias that tends to 1/0 for a perfect mirror), and
//  the roughness-aware Fresnel (which reduces to the plain Schlick form at roughness 0).
//  These mirror the bake shaders in shaders/*.frag.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include <cmath>  // std::isfinite, std::fabs

#include "renderer/Pbr.hpp"

using koi::fresnelSchlick;
using koi::fresnelSchlickRoughness;
using koi::geometrySchlickGGXIBL;
using koi::hammersley;
using koi::importanceSampleGGX;
using koi::integrateBRDF;
using koi::radicalInverseVdC;
using koi::Vec2;
using koi::Vec3;

TEST_CASE("Hammersley points fill the unit square and start at the origin") {
    // The first point is exactly (0, 0): i/N = 0 and the radical inverse of 0 is 0.
    const Vec2 first = hammersley(0u, 1024u);
    CHECK(first.x == doctest::Approx(0.0f));
    CHECK(first.y == doctest::Approx(0.0f));

    // Every point lies in [0,1)×[0,1), and the first coordinate is exactly i/N.
    const std::uint32_t n = 64u;
    for (std::uint32_t i = 0; i < n; ++i) {
        const Vec2 p = hammersley(i, n);
        CHECK(p.x == doctest::Approx(static_cast<float>(i) / static_cast<float>(n)));
        CHECK(p.x >= 0.0f);
        CHECK(p.x < 1.0f);
        CHECK(p.y >= 0.0f);
        CHECK(p.y < 1.0f);
    }

    // The radical inverse mirrors bits about the binary point: 1 -> 0.5, 2 -> 0.25,
    // 3 -> 0.75. A quick spot-check that the bit reversal is wired correctly.
    CHECK(radicalInverseVdC(1u) == doctest::Approx(0.5f));
    CHECK(radicalInverseVdC(2u) == doctest::Approx(0.25f));
    CHECK(radicalInverseVdC(3u) == doctest::Approx(0.75f));
}

TEST_CASE("importanceSampleGGX returns unit vectors in the upper hemisphere") {
    const Vec3 n{0.0f, 0.0f, 1.0f};

    for (std::uint32_t i = 0; i < 32u; ++i) {
        const Vec2 xi = hammersley(i, 32u);
        const Vec3 h  = importanceSampleGGX(xi, n, 0.5f);
        CHECK(koi::length(h) == doctest::Approx(1.0f));
        CHECK(koi::dot(h, n) >= 0.0f);  // sampled around N, never below the surface
    }

    // As roughness → 0 the lobe collapses onto the normal: every half-vector ≈ N.
    for (std::uint32_t i = 0; i < 16u; ++i) {
        const Vec2 xi = hammersley(i, 16u);
        const Vec3 h  = importanceSampleGGX(xi, n, 0.0f);
        CHECK(koi::dot(h, n) == doctest::Approx(1.0f));
    }
}

TEST_CASE("geometrySchlickGGXIBL uses the IBL remap (k = roughness^2/2)") {
    // At roughness 0, k = 0, so the term is nDotX / nDotX = 1 (no self-shadowing).
    CHECK(geometrySchlickGGXIBL(0.5f, 0.0f) == doctest::Approx(1.0f));

    // In [0,1] and shrinking as roughness grows (rougher = more masking).
    float prev = geometrySchlickGGXIBL(0.6f, 0.0f);
    for (float r = 0.1f; r <= 1.0f; r += 0.1f) {
        const float cur = geometrySchlickGGXIBL(0.6f, r);
        CHECK(cur >= 0.0f);
        CHECK(cur <= 1.0f);
        CHECK(cur <= prev);
        prev = cur;
    }
}

TEST_CASE("integrateBRDF is bounded and tends to (1,0) for a mirror") {
    // Every cell of the LUT is a scale/bias in [0,1], and finite.
    for (float nv = 0.1f; nv <= 1.0f; nv += 0.1f) {
        for (float r = 0.0f; r <= 1.0f; r += 0.25f) {
            const Vec2 lut = integrateBRDF(nv, r, /*samples=*/256u);
            CHECK(std::isfinite(lut.x));
            CHECK(std::isfinite(lut.y));
            CHECK(lut.x >= 0.0f);
            CHECK(lut.x <= 1.0f);
            CHECK(lut.y >= 0.0f);
            CHECK(lut.y <= 1.0f);
        }
    }

    // A perfect mirror (roughness 0) viewed head-on: the reflection passes through
    // essentially unweighted, so scale ≈ 1 and bias ≈ 0.
    const Vec2 mirror = integrateBRDF(1.0f, 0.0f, /*samples=*/1024u);
    CHECK(mirror.x == doctest::Approx(1.0f).epsilon(0.02f));
    CHECK(mirror.y == doctest::Approx(0.0f).epsilon(0.02f));
}

TEST_CASE("fresnelSchlickRoughness stays in [F0,1] and matches Schlick at roughness 0") {
    const float f0 = 0.04f;

    // Head-on (cosTheta = 1): reflectance is exactly F0, regardless of roughness.
    CHECK(fresnelSchlickRoughness(1.0f, f0, 0.0f) == doctest::Approx(f0));
    CHECK(fresnelSchlickRoughness(1.0f, f0, 0.8f) == doctest::Approx(f0));

    // At roughness 0 the grazing cap is max(1, F0) = 1, so it reduces to plain Schlick.
    for (float c = 0.0f; c <= 1.0f; c += 0.1f) {
        CHECK(fresnelSchlickRoughness(c, f0, 0.0f) == doctest::Approx(fresnelSchlick(c, f0)));
    }

    // Bounded below by F0 and above by 1 across angles and roughness.
    for (float r = 0.0f; r <= 1.0f; r += 0.2f) {
        for (float c = 0.0f; c <= 1.0f; c += 0.1f) {
            const float f = fresnelSchlickRoughness(c, f0, r);
            CHECK(f >= f0 - 1e-6f);
            CHECK(f <= 1.0f + 1e-6f);
        }
    }
}
