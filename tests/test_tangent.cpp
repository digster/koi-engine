// ============================================================================
//  test_tangent.cpp — the per-vertex tangent math for normal mapping (Step 13)
// ----------------------------------------------------------------------------
//  A tangent-space normal map is meaningless without a per-vertex tangent to orient
//  it. renderer/Tangents.hpp derives that tangent from positions + UVs; getting it
//  wrong corrupts every normal-mapped surface silently. These pure, headless checks
//  pin the key properties: the tangent points along increasing U, it comes out
//  orthonormal to the normal, and degenerate UVs fall back to something valid (never
//  a NaN). The GLSL side rebuilds the TBN from this tangent — same "CPU twin" pattern
//  as test_pbr / test_light.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides the main().
// ============================================================================
#include <doctest/doctest.h>

#include <cmath>

#include "renderer/Tangents.hpp"

using koi::orthonormalizeTangent;
using koi::triangleTangent;
using koi::Vec2;
using koi::Vec3;

namespace {
bool isFinite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
}  // namespace

TEST_CASE("triangleTangent points along the +U direction of the UV mapping") {
    // A flat triangle in the XY plane where U runs along +X and V along +Y. The
    // tangent (direction of increasing U) should therefore point along +X.
    const Vec3 p0{0, 0, 0}, p1{1, 0, 0}, p2{0, 1, 0};
    const Vec2 uv0{0, 0}, uv1{1, 0}, uv2{0, 1};
    const Vec3 t = triangleTangent(p0, p1, p2, uv0, uv1, uv2);

    REQUIRE(isFinite(t));
    const Vec3 tn = koi::normalize(t);
    CHECK(tn.x == doctest::Approx(1.0f));
    CHECK(tn.y == doctest::Approx(0.0f));
    CHECK(tn.z == doctest::Approx(0.0f));
}

TEST_CASE("triangleTangent tracks a rotated UV layout") {
    // Same geometry, but now U increases along +Y (the UVs are swapped). The tangent
    // must follow the UVs, not the geometry, so it should now point along +Y.
    const Vec3 p0{0, 0, 0}, p1{1, 0, 0}, p2{0, 1, 0};
    const Vec2 uv0{0, 0}, uv1{0, 1}, uv2{1, 0};
    const Vec3 tn = koi::normalize(triangleTangent(p0, p1, p2, uv0, uv1, uv2));
    CHECK(tn.x == doctest::Approx(0.0f));
    CHECK(tn.y == doctest::Approx(1.0f));
}

TEST_CASE("triangleTangent returns zero for a degenerate (zero-area) UV mapping") {
    // All three UVs identical → no U/V gradient → no usable tangent direction. The
    // helper must return {0,0,0} (not divide by zero) so the caller can skip it.
    const Vec3 p0{0, 0, 0}, p1{1, 0, 0}, p2{0, 1, 0};
    const Vec2 uv{0.5f, 0.5f};
    const Vec3 t = triangleTangent(p0, p1, p2, uv, uv, uv);
    CHECK(t.x == doctest::Approx(0.0f));
    CHECK(t.y == doctest::Approx(0.0f));
    CHECK(t.z == doctest::Approx(0.0f));
}

TEST_CASE("orthonormalizeTangent yields a unit tangent perpendicular to the normal") {
    const Vec3 n{0, 0, 1};
    // A tangent with a component along the normal: Gram-Schmidt should strip it out.
    const Vec3 raw{2.0f, 0.0f, 5.0f};
    const Vec3 t = orthonormalizeTangent(raw, n);

    CHECK(isFinite(t));
    CHECK(koi::length(t) == doctest::Approx(1.0f));       // unit length
    CHECK(koi::dot(t, n) == doctest::Approx(0.0f));       // perpendicular to N
    // The surviving in-plane part was along +X, so the result points along +X.
    CHECK(t.x == doctest::Approx(1.0f));
}

TEST_CASE("orthonormalizeTangent falls back to a valid perpendicular for a bad tangent") {
    const Vec3 n{0, 1, 0};
    // A zero tangent (all UVs were degenerate) must not produce a NaN — the helper
    // returns an arbitrary unit vector perpendicular to the normal instead.
    const Vec3 t = orthonormalizeTangent(Vec3{0, 0, 0}, n);
    CHECK(isFinite(t));
    CHECK(koi::length(t) == doctest::Approx(1.0f));
    CHECK(koi::dot(t, n) == doctest::Approx(0.0f));

    // A tangent parallel to the normal collapses under Gram-Schmidt → same fallback.
    const Vec3 t2 = orthonormalizeTangent(Vec3{0, 3, 0}, n);
    CHECK(isFinite(t2));
    CHECK(koi::length(t2) == doctest::Approx(1.0f));
    CHECK(koi::dot(t2, n) == doctest::Approx(0.0f));
}
