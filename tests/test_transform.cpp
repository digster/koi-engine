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

namespace {

// Assert two 4x4 matrices match element-for-element (used by the fromMatrix
// round-trip tests, which compare placements by their baked matrix).
void checkMat4(const Mat4& got, const Mat4& want) {
    for (int i = 0; i < 16; ++i) {
        CHECK(got.m[i] == doctest::Approx(want.m[i]));
    }
}

}  // namespace

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
    t.scale    = {2, 2, 2};
    t.rotation = Quat::fromEuler({0, 0, radians(90.0f)});
    t.position = {10, 0, 0};

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

TEST_CASE("fromMatrix inverts localMatrix (round-trip, incl. non-uniform scale)") {
    // The Step 25 glTF importer decomposes a baked node matrix back into TRS. For
    // representative placements, fromMatrix(t.localMatrix()) must rebuild the SAME
    // matrix. We compare the baked matrices (not the raw TRS) because a decomposition
    // is only unique up to the usual sign ambiguities.
    Transform samples[3];
    samples[0].position = {3, -4, 5};
    samples[0].rotation = Quat::fromEuler({radians(25.0f), radians(-40.0f), radians(15.0f)});
    samples[0].scale    = {2, 2, 2};                  // uniform scale

    samples[1].position = {-1, 2, -3};
    samples[1].rotation = Quat::fromAxisAngle(Vec3{0, 1, 0}, radians(90.0f));
    samples[1].scale    = {0.5f, 3.0f, 1.25f};        // non-uniform scale

    samples[2].position = {0, 0, 0};
    samples[2].rotation = Quat::fromEuler({radians(72.0f), radians(-22.0f), 0.0f});
    samples[2].scale    = {1.6f, 1.6f, 1.6f};         // the helmet's placement

    for (const Transform& t : samples) {
        const Transform d = Transform::fromMatrix(t.localMatrix());
        checkMat4(d.localMatrix(), t.localMatrix());
    }
}

TEST_CASE("fromMatrix handles a mirrored (negative-scale) node") {
    // A negative scale flips handedness. The decomposition must fold that flip into
    // the scale rather than invent an impossible left-handed rotation, so the rebuilt
    // matrix still matches even though the recovered scale axis may differ.
    Transform t;
    t.position = {1, 1, 1};
    t.rotation = Quat::fromAxisAngle(Vec3{0, 1, 0}, radians(30.0f));
    t.scale    = {-1.0f, 1.0f, 1.0f};
    const Transform d = Transform::fromMatrix(t.localMatrix());
    checkMat4(d.localMatrix(), t.localMatrix());
}
