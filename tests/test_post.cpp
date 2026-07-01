// ============================================================================
//  test_post.cpp — tests for the pure post-processing helpers (Step 10)
// ----------------------------------------------------------------------------
//  The post-processing effects themselves run on the GPU, but the small bits of
//  math around them are pure and worth pinning down: the half-resolution sizing of
//  the bloom targets, the luma weights, and the shape of the ACES tone-map curve.
//  No GPU or window needed — these run headlessly like the rest of the suite.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include "renderer/PostProcess.hpp"

using koi::acesToneMap;
using koi::halfExtent;
using koi::luminance;

TEST_CASE("halfExtent halves an extent but never drops below 1") {
    CHECK(halfExtent(1280) == 640);
    CHECK(halfExtent(720) == 360);
    CHECK(halfExtent(2) == 1);
    CHECK(halfExtent(3) == 1);  // integer division floors
    CHECK(halfExtent(1) == 1);  // clamped: a half-res target is still at least 1px
    CHECK(halfExtent(0) == 1);  // clamped: never zero (would be an invalid texture)
}

TEST_CASE("luminance weights green most and is normalized to white = 1") {
    CHECK(luminance(1.0f, 1.0f, 1.0f) == doctest::Approx(1.0f));
    // Equal-magnitude channels: green must read brighter than red, red than blue.
    CHECK(luminance(0.0f, 1.0f, 0.0f) > luminance(1.0f, 0.0f, 0.0f));
    CHECK(luminance(1.0f, 0.0f, 0.0f) > luminance(0.0f, 0.0f, 1.0f));
    CHECK(luminance(0.0f, 0.0f, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("acesToneMap maps black to black and stays within [0,1]") {
    CHECK(acesToneMap(0.0f) == doctest::Approx(0.0f));
    // Even a very bright HDR input saturates just under 1, never above (no clipping
    // to flat white, no out-of-range values a display can't show).
    const float big = acesToneMap(1000.0f);
    CHECK(big <= 1.0f);
    CHECK(big > 0.9f);
}

TEST_CASE("acesToneMap is monotonically increasing") {
    // Brighter input must never produce a darker output across the range.
    float prev = acesToneMap(0.0f);
    for (float x = 0.05f; x <= 20.0f; x += 0.05f) {
        const float cur = acesToneMap(x);
        CHECK(cur >= prev);
        prev = cur;
    }
}
