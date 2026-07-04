// ============================================================================
//  test_geometry.cpp — unit tests for the Step 19 geometry utilities
// ----------------------------------------------------------------------------
//  Like the rest of the math, these primitives are pure (no GPU, no window), so
//  a headless test can pin down their exact behaviour on any machine. That's the
//  whole reason the geometry layer ships as isolated math BEFORE the systems that
//  consume it (culling, picking, physics): a sign error in a frustum plane or a
//  ray test is otherwise a maddening visual bug with no error message.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include <cmath>

#include "math/Geometry.hpp"
#include "math/Mat4.hpp"
#include "math/Vec.hpp"

using namespace koi;

// Apply a matrix to a DIRECTION (w = 0 drops translation) and read back a Vec3.
// Equivalent to mat3(m) * v — used by the normal-matrix test below.
static Vec3 xformDir(const Mat4& m, Vec3 v) {
    const Vec4 r = m * Vec4{v.x, v.y, v.z, 0.0f};
    return {r.x, r.y, r.z};
}

// ----------------------------------------------------------------------------
//  Mat4 inverse / transpose
// ----------------------------------------------------------------------------
TEST_CASE("inverse(M) * M is the identity (affine TRS matrix)") {
    const Mat4 M = translation({1, 2, 3}) * rotationZ(radians(30.0f)) * scaling({2, 0.5f, 1.5f});
    const Mat4 I = inverse(M) * M;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float expected = (r == c) ? 1.0f : 0.0f;
            CHECK(I.at(r, c) == doctest::Approx(expected).epsilon(1e-5));
        }
    }
}

TEST_CASE("inverse(M) * M is the identity (perspective matrix)") {
    // Projection matrices have a very different structure (and a small
    // determinant), so they're a good stress test for the general inverse.
    const Mat4 P = perspective(radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const Mat4 I = inverse(P) * P;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float expected = (r == c) ? 1.0f : 0.0f;
            CHECK(I.at(r, c) == doctest::Approx(expected).epsilon(1e-3));
        }
    }
}

TEST_CASE("inverse of a singular matrix fails soft (returns identity)") {
    // A zero scale collapses an axis → no inverse. We return identity, not NaNs.
    const Mat4 singular = scaling({1, 0, 1});
    const Mat4 inv = inverse(singular);
    CHECK(inv.at(0, 0) == doctest::Approx(1.0f));
    CHECK(inv.at(1, 1) == doctest::Approx(1.0f));
    CHECK(inv.at(2, 2) == doctest::Approx(1.0f));
}

TEST_CASE("transpose swaps (r,c) with (c,r)") {
    const Mat4 T = translation({1, 2, 3});   // translation lives in the last column
    const Mat4 Tt = transpose(T);
    // Column-3 translation becomes row-3.
    CHECK(Tt.at(3, 0) == doctest::Approx(1.0f));
    CHECK(Tt.at(3, 1) == doctest::Approx(2.0f));
    CHECK(Tt.at(3, 2) == doctest::Approx(3.0f));
    // Original last-column entries are now zero (they moved).
    CHECK(Tt.at(0, 3) == doctest::Approx(0.0f));
}

// ----------------------------------------------------------------------------
//  AABB
// ----------------------------------------------------------------------------
TEST_CASE("Aabb contains, center and extents") {
    const Aabb box{{0, 0, 0}, {2, 4, 6}};
    CHECK(box.contains({1, 2, 3}));
    CHECK_FALSE(box.contains({3, 0, 0}));  // outside on x
    CHECK(box.center().y == doctest::Approx(2.0f));
    CHECK(box.extents().z == doctest::Approx(3.0f));
}

TEST_CASE("Aabb expand from empty, and merge") {
    Aabb a = Aabb::empty();
    a.expand({1, 1, 1});
    a.expand({-1, 3, 0});
    CHECK(a.min.x == doctest::Approx(-1.0f));
    CHECK(a.max.y == doctest::Approx(3.0f));

    Aabb b{{2, 2, 2}, {3, 3, 3}};
    a.merge(b);
    CHECK(a.min.x == doctest::Approx(-1.0f));
    CHECK(a.max.x == doctest::Approx(3.0f));
    CHECK(a.max.z == doctest::Approx(3.0f));
}

TEST_CASE("Aabb transformed by a rotation grows to re-bound the tilted box") {
    // A unit cube [-1,1]^3 rotated 45° about Z: its corner (1,1) swings onto the
    // Y axis at distance sqrt(2), so the axis-aligned bounds widen to ±sqrt(2)
    // in x and y while z is untouched. This is the local→world bounds step culling
    // relies on.
    const Aabb unit{{-1, -1, -1}, {1, 1, 1}};
    const Aabb world = unit.transformed(rotationZ(radians(45.0f)));
    const float s2 = std::sqrt(2.0f);
    CHECK(world.max.x == doctest::Approx(s2));
    CHECK(world.min.x == doctest::Approx(-s2));
    CHECK(world.max.y == doctest::Approx(s2));
    CHECK(world.max.z == doctest::Approx(1.0f));  // unchanged along the spin axis
}

TEST_CASE("Aabb transformed by a translation moves the box") {
    const Aabb unit{{-1, -1, -1}, {1, 1, 1}};
    const Aabb moved = unit.transformed(translation({10, 0, 0}));
    CHECK(moved.min.x == doctest::Approx(9.0f));
    CHECK(moved.max.x == doctest::Approx(11.0f));
}

// ----------------------------------------------------------------------------
//  Ray vs AABB (the picking primitive)
// ----------------------------------------------------------------------------
TEST_CASE("Ray hits an AABB straight on") {
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    const Ray ray{{0, 0, 5}, {0, 0, -1}};  // from +Z looking toward the box
    float t = 0.0f;
    CHECK(intersect(ray, box, t));
    CHECK(t == doctest::Approx(4.0f));  // enters the front face at z = 1
}

TEST_CASE("Ray misses an AABB it points past") {
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    const Ray ray{{5, 5, 5}, {0, 0, -1}};  // stays at x=y=5, never enters the box
    float t = 0.0f;
    CHECK_FALSE(intersect(ray, box, t));
}

TEST_CASE("Ray does not hit a box behind its origin") {
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    const Ray ray{{0, 0, 5}, {0, 0, 1}};  // pointing AWAY from the box
    float t = 0.0f;
    CHECK_FALSE(intersect(ray, box, t));
}

TEST_CASE("Ray whose origin is inside the box reports t = 0") {
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    const Ray ray{{0, 0, 0}, {0, 0, -1}};
    float t = -1.0f;
    CHECK(intersect(ray, box, t));
    CHECK(t == doctest::Approx(0.0f));
}

// ----------------------------------------------------------------------------
//  Plane
// ----------------------------------------------------------------------------
TEST_CASE("Plane signed distance reports which side a point is on") {
    const Plane ground{{0, 1, 0}, 0.0f};  // the y = 0 plane, normal pointing up
    CHECK(ground.signedDistance({0, 5, 0}) == doctest::Approx(5.0f));    // above
    CHECK(ground.signedDistance({0, -3, 0}) == doctest::Approx(-3.0f));  // below
}

TEST_CASE("Plane normalized rescales to a unit normal (true distances)") {
    // Same plane described with a length-2 normal: y = 2. normalized() should give
    // unit normal and the correct signed distance.
    const Plane p = Plane{{0, 2, 0}, -4.0f}.normalized();
    CHECK(length(p.normal) == doctest::Approx(1.0f));
    CHECK(p.signedDistance({0, 2, 0}) == doctest::Approx(0.0f));  // point ON the plane
    CHECK(p.signedDistance({0, 5, 0}) == doctest::Approx(3.0f));  // 3 units above
}

// ----------------------------------------------------------------------------
//  Frustum (built with Koi's z ∈ [0,1] projection)
// ----------------------------------------------------------------------------
TEST_CASE("Frustum accepts a box in view and rejects boxes outside it") {
    // Camera at (0,0,5) looking at the origin down its own -Z, 90° square FOV.
    const Mat4 view = lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = perspective(radians(90.0f), 1.0f, 0.1f, 100.0f);
    const Frustum f = Frustum::fromViewProjection(proj * view);

    // A small box at the origin is squarely inside.
    CHECK(f.intersectsAabb(Aabb{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}));

    // A box BEHIND the camera (z = 20, camera looks toward -Z from z = 5) is
    // rejected by the near plane — this is exactly the case the z∈[0,1] near-plane
    // extraction (row2 alone) must get right.
    CHECK_FALSE(f.intersectsAabb(Aabb{{-0.5f, -0.5f, 19.5f}, {0.5f, 0.5f, 20.5f}}));

    // A box far off to the side is rejected by the left/right planes.
    CHECK_FALSE(f.intersectsAabb(Aabb{{49.5f, -0.5f, 0.0f}, {50.5f, 0.5f, 1.0f}}));
}

TEST_CASE("Frustum reports a straddling box as visible") {
    const Mat4 view = lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = perspective(radians(90.0f), 1.0f, 0.1f, 100.0f);
    const Frustum f = Frustum::fromViewProjection(proj * view);

    // A huge box that spans from well inside to well outside must NOT be culled.
    CHECK(f.intersectsAabb(Aabb{{-100, -100, -1}, {100, 100, 1}}));
}

// ----------------------------------------------------------------------------
//  Normal matrix — the Step 19 render-path fix, tested as pure CPU math.
// ----------------------------------------------------------------------------
TEST_CASE("normal matrix keeps normals perpendicular under non-uniform scale") {
    // A rotation composed with a NON-uniform scale — the case where naïvely using
    // the model matrix on a normal tilts it off the surface.
    const Mat4 model = rotationZ(radians(30.0f)) * scaling({2.0f, 1.0f, 3.0f});
    const Mat4 normalMatrix = transpose(inverse(model));  // what the shader uploads

    // A surface with this normal and two in-surface tangents (both ⟂ N).
    const Vec3 N  = normalize({1, 1, 0});
    const Vec3 T1 = {1, -1, 0};   // dot(N,T1) = 0
    const Vec3 T2 = {0, 0, 1};    // dot(N,T2) = 0

    const Vec3 Ncorrect = xformDir(normalMatrix, N);
    const Vec3 T1p = xformDir(model, T1);
    const Vec3 T2p = xformDir(model, T2);

    // With the normal matrix the transformed normal stays perpendicular to the
    // transformed surface directions — the property lighting depends on.
    CHECK(dot(Ncorrect, T1p) == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(dot(Ncorrect, T2p) == doctest::Approx(0.0f).epsilon(1e-5));

    // And confirm the naïve mat3(model)*N breaks perpendicularity (this is WHY the
    // normal matrix exists): the dot product is clearly non-zero.
    const Vec3 Nnaive = xformDir(model, N);
    CHECK(std::abs(dot(Nnaive, T1p)) > 0.5f);
}
