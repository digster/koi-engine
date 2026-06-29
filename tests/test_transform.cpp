// ============================================================================
//  test_transform.cpp — unit tests for Mat4::scaling and Transform::localMatrix
// ----------------------------------------------------------------------------
//  Like the rest of our math, a Transform is pure and GPU-free, so we can assert
//  exact placements on any machine. The interesting property to pin down is the
//  T·R·S ORDER: scale first (in the object's own space), then rotate, then
//  translate. A worked example below proves we built that order correctly.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include "math/Mat4.hpp"
#include "math/Vec.hpp"
#include "scene/Transform.hpp"

using namespace koi;

TEST_CASE("scaling stretches a point per axis (and leaves w alone)") {
    const Vec4 p = scaling(Vec3{2, 3, 4}) * Vec4{1, 1, 1, 1};
    CHECK(p.x == doctest::Approx(2.0f));
    CHECK(p.y == doctest::Approx(3.0f));
    CHECK(p.z == doctest::Approx(4.0f));
    CHECK(p.w == doctest::Approx(1.0f));
}

TEST_CASE("a default Transform is the identity") {
    const Transform t;  // position 0, rotation 0, scale 1
    const Vec4 v = t.localMatrix() * Vec4{5, -2, 3, 1};
    CHECK(v.x == doctest::Approx(5.0f));
    CHECK(v.y == doctest::Approx(-2.0f));
    CHECK(v.z == doctest::Approx(3.0f));
}

TEST_CASE("Transform position translates a point") {
    Transform t;
    t.position = {10, 0, -5};
    const Vec4 v = t.localMatrix() * Vec4{1, 2, 3, 1};
    CHECK(v.x == doctest::Approx(11.0f));
    CHECK(v.y == doctest::Approx(2.0f));
    CHECK(v.z == doctest::Approx(-2.0f));
}

TEST_CASE("Transform scale resizes around the object's origin") {
    Transform t;
    t.scale = {2, 2, 2};
    const Vec4 v = t.localMatrix() * Vec4{1, 1, 1, 1};
    CHECK(v.x == doctest::Approx(2.0f));
    CHECK(v.y == doctest::Approx(2.0f));
    CHECK(v.z == doctest::Approx(2.0f));
}

TEST_CASE("Transform applies scale, then rotation, then translation") {
    // Scale 2x, rotate 90° about Z (+X -> +Y), then translate by (10, 0, 0).
    Transform t;
    t.scale         = {2, 2, 2};
    t.rotationEuler = {0, 0, radians(90.0f)};
    t.position      = {10, 0, 0};

    // Trace the model-space point (1, 0, 0):
    //   scale     -> (2, 0, 0)
    //   rotateZ90 -> (0, 2, 0)
    //   translate -> (10, 2, 0)
    // If the order were wrong (e.g. translate before scale) we'd get a different
    // answer, which is exactly what this test guards against.
    const Vec4 v = t.localMatrix() * Vec4{1, 0, 0, 1};
    CHECK(v.x == doctest::Approx(10.0f));
    CHECK(v.y == doctest::Approx(2.0f));
    CHECK(v.z == doctest::Approx(0.0f));
}
