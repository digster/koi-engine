// ============================================================================
//  test_debug_draw.cpp — unit tests for the Step 22 debug-line collector
// ----------------------------------------------------------------------------
//  DebugDraw is pure (no GPU, no window): it only turns shapes into a flat list
//  of coloured line-list vertices. That makes its geometry exactly testable
//  headlessly — a wrong corner order or a flipped NDC z-convention in frustum()
//  would otherwise be an invisible "the wireframe looks slightly off" bug. The
//  GPU side (the line pipeline + per-frame upload) is verified end-to-end by the
//  KOI_CAPTURE frame instead.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include "math/Geometry.hpp"
#include "math/Mat4.hpp"
#include "math/Vec.hpp"
#include "renderer/DebugDraw.hpp"

using namespace koi;

// Approximate Vec3 equality for the float comparisons below.
static bool vec3Approx(const Vec3& a, const Vec3& b, float eps = 1e-4f) {
    return a.x == doctest::Approx(b.x).epsilon(eps) &&
           a.y == doctest::Approx(b.y).epsilon(eps) &&
           a.z == doctest::Approx(b.z).epsilon(eps);
}

TEST_CASE("line() appends two vertices carrying the colour") {
    DebugDraw dd;
    CHECK(dd.empty());

    dd.line({1, 2, 3}, {4, 5, 6}, {0.2f, 0.4f, 0.6f});
    REQUIRE(dd.size() == 2);
    const auto v = dd.vertices();
    CHECK(vec3Approx(v[0].position, {1, 2, 3}));
    CHECK(vec3Approx(v[1].position, {4, 5, 6}));
    CHECK(vec3Approx(v[0].color, {0.2f, 0.4f, 0.6f}));
    CHECK(vec3Approx(v[1].color, {0.2f, 0.4f, 0.6f}));
}

TEST_CASE("clear() empties the list but is reusable") {
    DebugDraw dd;
    dd.line({0, 0, 0}, {1, 1, 1}, {1, 1, 1});
    dd.clear();
    CHECK(dd.empty());
    CHECK(dd.size() == 0);
    dd.line({0, 0, 0}, {1, 1, 1}, {1, 1, 1});
    CHECK(dd.size() == 2);
}

TEST_CASE("box() draws the 12 edges of an AABB") {
    DebugDraw dd;
    const Aabb box{{-1, -2, -3}, {1, 2, 3}};
    dd.box(box, {1, 1, 1});

    // 12 edges × 2 vertices.
    REQUIRE(dd.size() == 24);
    const auto v = dd.vertices();

    // The first edge (kBoxEdges[0] = {0,1}) runs along X between the all-min corner
    // and the (max.x, min.y, min.z) corner — pins the corner bit-convention.
    CHECK(vec3Approx(v[0].position, {-1, -2, -3}));
    CHECK(vec3Approx(v[1].position, { 1, -2, -3}));

    // Every vertex must be a genuine box corner: each coordinate equals the box's
    // min or max on that axis.
    for (const DebugVertex& dv : v) {
        CHECK((dv.position.x == doctest::Approx(-1) || dv.position.x == doctest::Approx(1)));
        CHECK((dv.position.y == doctest::Approx(-2) || dv.position.y == doctest::Approx(2)));
        CHECK((dv.position.z == doctest::Approx(-3) || dv.position.z == doctest::Approx(3)));
    }
}

TEST_CASE("frustum(identity) unprojects to the NDC cube (z in [0,1])") {
    // With an identity view-projection, inverse() is identity too, so unprojecting
    // the NDC cube corners yields the NDC cube itself. This pins BOTH the 12-edge
    // topology and Koi's z ∈ [0,1] depth convention (near at 0, far at 1) — NOT
    // OpenGL's [-1,1], which would put the near face at z = -1.
    DebugDraw dd;
    dd.frustum(Mat4::identity(), {1, 1, 1});
    REQUIRE(dd.size() == 24);

    for (const DebugVertex& dv : dd.vertices()) {
        CHECK((dv.position.x == doctest::Approx(-1) || dv.position.x == doctest::Approx(1)));
        CHECK((dv.position.y == doctest::Approx(-1) || dv.position.y == doctest::Approx(1)));
        CHECK((dv.position.z == doctest::Approx(0) || dv.position.z == doctest::Approx(1)));
    }
}

TEST_CASE("frustum() unprojects a real perspective to near/far planes") {
    // Camera at the origin looking down -Z (lookAt gives identity view), so world
    // space == view space and the frustum corners land at exactly z = -near and
    // z = -far. This exercises the perspective divide (far corners have clip.w ≫ 1).
    const float near = 0.1f, far = 20.0f;
    const Mat4 view = lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    const Mat4 viewProj = perspective(radians(60.0f), 16.0f / 9.0f, near, far) * view;

    DebugDraw dd;
    dd.frustum(viewProj, {1, 1, 1});
    REQUIRE(dd.size() == 24);

    bool sawNear = false, sawFar = false;
    for (const DebugVertex& dv : dd.vertices()) {
        const bool onNear = dv.position.z == doctest::Approx(-near).epsilon(1e-3);
        const bool onFar  = dv.position.z == doctest::Approx(-far).epsilon(1e-3);
        CHECK((onNear || onFar));  // every corner lies on the near OR the far plane
        sawNear = sawNear || onNear;
        sawFar  = sawFar  || onFar;
    }
    CHECK(sawNear);
    CHECK(sawFar);
}

TEST_CASE("cross() draws three axis-aligned segments through the centre") {
    DebugDraw dd;
    dd.cross({1, 2, 3}, 2.0f, {0.5f, 0.5f, 0.5f});  // half-length 1 to each side
    REQUIRE(dd.size() == 6);
    const auto v = dd.vertices();
    // X segment first.
    CHECK(vec3Approx(v[0].position, {0, 2, 3}));
    CHECK(vec3Approx(v[1].position, {2, 2, 3}));
    // Y segment.
    CHECK(vec3Approx(v[2].position, {1, 1, 3}));
    CHECK(vec3Approx(v[3].position, {1, 3, 3}));
    // Z segment.
    CHECK(vec3Approx(v[4].position, {1, 2, 2}));
    CHECK(vec3Approx(v[5].position, {1, 2, 4}));
}

TEST_CASE("ray() normalizes the direction and scales by length") {
    DebugDraw dd;
    dd.ray({0, 0, 0}, {0, 0, -2}, 5.0f, {1, 0, 0});  // dir normalizes to (0,0,-1)
    REQUIRE(dd.size() == 2);
    const auto v = dd.vertices();
    CHECK(vec3Approx(v[0].position, {0, 0, 0}));
    CHECK(vec3Approx(v[1].position, {0, 0, -5}));
}
