// ============================================================================
//  test_light.cpp — tests for the pure light helpers (Step 11)
// ----------------------------------------------------------------------------
//  The lighting itself runs on the GPU, but the small pieces of math around it are
//  pure and worth pinning down: the distance ATTENUATION curve (windowed
//  inverse-square), the SPOT-cone falloff, and how many lights the renderer will
//  upload (enabled, capped at MAX_LIGHTS). These are the CPU twins of the shader
//  math in triangle.frag — testing them here documents the intended shape and
//  guards against drift. No GPU or window needed.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "scene/Light.hpp"

using koi::activeLightCount;
using koi::attenuation;
using koi::Light;
using koi::LightType;
using koi::MAX_LIGHTS;
using koi::spotFactor;

TEST_CASE("attenuation starts at ~1, falls off, and reaches 0 at range") {
    constexpr float range = 10.0f;

    // At the light's own position the windowed inverse-square term is exactly 1.
    CHECK(attenuation(0.0f, range) == doctest::Approx(1.0f));

    // Exactly at (and beyond) the range the window forces the light to 0, so the
    // shader can safely ignore anything farther than `range`.
    CHECK(attenuation(range, range) == doctest::Approx(0.0f));
    CHECK(attenuation(range + 5.0f, range) == doctest::Approx(0.0f));

    // A zero/negative range is a disabled light — no contribution anywhere.
    CHECK(attenuation(1.0f, 0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("attenuation is monotonically decreasing with distance") {
    constexpr float range = 12.0f;
    float prev = attenuation(0.0f, range);
    for (float d = 0.1f; d <= range; d += 0.1f) {
        const float cur = attenuation(d, range);
        CHECK(cur <= prev);         // never brighter as we move away
        CHECK(cur >= 0.0f);
        CHECK(cur <= 1.0f);
        prev = cur;
    }
}

TEST_CASE("spotFactor is 1 inside the inner cone and 0 outside the outer cone") {
    const float innerCos = std::cos(10.0f * 3.14159265f / 180.0f);  // cos(10°)
    const float outerCos = std::cos(20.0f * 3.14159265f / 180.0f);  // cos(20°)

    // A tiny angle from the axis (large cosine) is well inside the inner cone.
    CHECK(spotFactor(std::cos(5.0f * 3.14159265f / 180.0f), innerCos, outerCos)
          == doctest::Approx(1.0f));

    // A wide angle (small cosine) is past the outer cone → fully dark.
    CHECK(spotFactor(std::cos(25.0f * 3.14159265f / 180.0f), innerCos, outerCos)
          == doctest::Approx(0.0f));

    // Between the two cones the edge softens to a value strictly in (0, 1).
    const float mid = spotFactor(std::cos(15.0f * 3.14159265f / 180.0f), innerCos, outerCos);
    CHECK(mid > 0.0f);
    CHECK(mid < 1.0f);
}

TEST_CASE("spotFactor ramps monotonically across the cone edge") {
    const float innerCos = std::cos(10.0f * 3.14159265f / 180.0f);
    const float outerCos = std::cos(20.0f * 3.14159265f / 180.0f);
    // Sweep the angle from wide (dark) to narrow (bright); the factor must never
    // decrease as the fragment moves toward the spot's axis.
    float prev = 0.0f;
    for (float deg = 25.0f; deg >= 5.0f; deg -= 1.0f) {
        const float cur = spotFactor(std::cos(deg * 3.14159265f / 180.0f), innerCos, outerCos);
        CHECK(cur >= prev);
        prev = cur;
    }
}

TEST_CASE("activeLightCount counts enabled lights and caps at MAX_LIGHTS") {
    std::vector<Light> lights;

    // Empty list → no active lights.
    CHECK(activeLightCount(lights) == 0);

    // Three lights, one disabled → two active.
    lights.resize(3);
    lights[0].enabled = true;
    lights[1].enabled = false;
    lights[2].enabled = true;
    CHECK(activeLightCount(lights) == 2);

    // More enabled lights than the shader array can hold → clamped to MAX_LIGHTS.
    std::vector<Light> many(MAX_LIGHTS + 4);  // all enabled by default
    CHECK(activeLightCount(many) == MAX_LIGHTS);
}
