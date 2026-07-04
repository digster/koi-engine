// ============================================================================
//  test_quat.cpp — unit tests for the hand-rolled quaternion (Step 18)
// ----------------------------------------------------------------------------
//  Quaternions are pure math (no GPU, no window), so like the rest of src/math
//  they're the ideal thing to pin down exactly. The most important property here
//  is BEHAVIOUR-PRESERVATION: Quat::fromEuler must produce the very same rotation
//  the old Transform did (rotationZ * rotationY * rotationX), so swapping the
//  storage type left every existing scene looking identical. We also verify the
//  quaternion→matrix and vector-rotation paths agree, and that slerp walks the
//  shortest arc.
//
//  Because q and -q denote the SAME rotation, we usually compare rotations by
//  their EFFECT (rotate a probe vector / compare the matrix), not component-wise.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "math/Vec.hpp"

using namespace koi;

namespace {

// Assert two Vec3s match (component-wise, with float tolerance).
void checkVec3(Vec3 got, Vec3 want) {
    CHECK(got.x == doctest::Approx(want.x));
    CHECK(got.y == doctest::Approx(want.y));
    CHECK(got.z == doctest::Approx(want.z));
}

// Assert two 4x4 matrices match element-for-element.
void checkMat4(const Mat4& got, const Mat4& want) {
    for (int i = 0; i < 16; ++i) {
        CHECK(got.m[i] == doctest::Approx(want.m[i]));
    }
}

}  // namespace

TEST_CASE("a default quaternion is the identity rotation") {
    const Quat q;  // {0, 0, 0, 1}
    CHECK(q.x == doctest::Approx(0.0f));
    CHECK(q.w == doctest::Approx(1.0f));
    // It rotates nothing and its matrix is the identity.
    checkVec3(q.rotate(Vec3{5, -2, 3}), Vec3{5, -2, 3});
    checkMat4(q.toMat4(), Mat4::identity());
}

TEST_CASE("fromAxisAngle(Y, 90) matches rotationY and maps +X to -Z") {
    const Quat q = Quat::fromAxisAngle(Vec3{0, 1, 0}, radians(90.0f));

    // Right-handed: a +90° turn about Y sends +X to -Z (same as rotationY).
    checkVec3(q.rotate(Vec3{1, 0, 0}), Vec3{0, 0, -1});

    // The generated matrix must equal the already-trusted Mat4 rotation, tying
    // the new quaternion path to the existing, separately-tested one.
    checkMat4(q.toMat4(), rotationY(radians(90.0f)));
}

TEST_CASE("fromAxisAngle normalizes its axis") {
    // A non-unit axis must still yield a unit quaternion / pure rotation: a probe
    // vector's length is preserved and the matrix matches the normalized axis.
    const Quat q = Quat::fromAxisAngle(Vec3{0, 5, 0}, radians(90.0f));
    CHECK(length(q) == doctest::Approx(1.0f));
    checkVec3(q.rotate(Vec3{1, 0, 0}), Vec3{0, 0, -1});
}

TEST_CASE("fromEuler reproduces the old Z*Y*X Euler rotation (behaviour-preserving)") {
    // This is the guarantee that made the Transform swap invisible: for arbitrary
    // Euler angles, fromEuler(e).toMat4() must equal rotationZ*rotationY*rotationX.
    const Vec3 samples[] = {
        {radians(90.0f), 0.0f, 0.0f},                  // torus stand-up
        {radians(72.0f), radians(-22.0f), 0.0f},       // helmet upright
        {radians(30.0f), radians(45.0f), radians(60.0f)},
        {radians(-15.0f), radians(120.0f), radians(-80.0f)},
    };
    for (const Vec3 e : samples) {
        const Mat4 fromEuler = Quat::fromEuler(e).toMat4();
        const Mat4 fromMats  = rotationZ(e.z) * rotationY(e.y) * rotationX(e.x);
        checkMat4(fromEuler, fromMats);
    }
}

TEST_CASE("the Hamilton product composes rotations like matrix multiply") {
    const Quat a = Quat::fromAxisAngle(Vec3{0, 1, 0}, radians(90.0f));
    const Quat b = Quat::fromAxisAngle(Vec3{1, 0, 0}, radians(45.0f));

    // (a*b) as a matrix must equal a.toMat4() * b.toMat4() — i.e. quaternion
    // multiply is a homomorphism onto matrix multiply (b applied first).
    checkMat4((a * b).toMat4(), a.toMat4() * b.toMat4());

    // And rotating by (a*b) equals rotating by b, then by a.
    const Vec3 v{1, 2, 3};
    checkVec3((a * b).rotate(v), a.rotate(b.rotate(v)));
}

TEST_CASE("inverse undoes a rotation") {
    const Quat q = Quat::fromAxisAngle(Vec3{0.3f, 1.0f, -0.5f}, radians(50.0f));

    // q composed with its inverse is the identity rotation.
    checkMat4((q * q.inverse()).toMat4(), Mat4::identity());

    // Rotating a vector then rotating back returns the original.
    const Vec3 v{2, -1, 4};
    checkVec3(q.inverse().rotate(q.rotate(v)), v);
}

TEST_CASE("normalize returns a unit quaternion") {
    // Deliberately non-unit input (a scaled identity): normalize must scale it back.
    const Quat scaled{0.0f, 0.0f, 0.0f, 4.0f};
    const Quat n = normalize(scaled);
    CHECK(length(n) == doctest::Approx(1.0f));
    checkMat4(n.toMat4(), Mat4::identity());
}

TEST_CASE("rotate(v) agrees with toMat4() * v") {
    const Quat q = Quat::fromEuler({radians(20.0f), radians(-35.0f), radians(70.0f)});
    const Vec3 v{1.5f, -2.0f, 0.75f};

    const Vec3 viaQuat = q.rotate(v);
    const Vec4 viaMat  = q.toMat4() * Vec4{v.x, v.y, v.z, 0.0f};  // w=0 → direction
    checkVec3(viaQuat, Vec3{viaMat.x, viaMat.y, viaMat.z});
}

TEST_CASE("slerp hits its endpoints") {
    const Quat a = Quat::fromAxisAngle(Vec3{0, 1, 0}, radians(10.0f));
    const Quat b = Quat::fromAxisAngle(Vec3{0, 1, 0}, radians(80.0f));
    const Vec3 probe{1, 0, 0};

    // t = 0 gives a, t = 1 gives b (compared by their effect on a probe vector).
    checkVec3(slerp(a, b, 0.0f).rotate(probe), a.rotate(probe));
    checkVec3(slerp(a, b, 1.0f).rotate(probe), b.rotate(probe));
}

TEST_CASE("slerp's midpoint is the true half-way rotation") {
    // Half-way between 0° and 90° about Y is 45° about Y. A +45° turn sends +X to
    // (cos45, 0, -sin45). The result must also stay on the unit sphere.
    const Quat mid = slerp(Quat::identity(),
                           Quat::fromAxisAngle(Vec3{0, 1, 0}, radians(90.0f)), 0.5f);
    CHECK(length(mid) == doctest::Approx(1.0f));
    const float k = 0.70710678f;  // cos/sin 45°
    checkVec3(mid.rotate(Vec3{1, 0, 0}), Vec3{k, 0.0f, -k});
}

TEST_CASE("slerp takes the shortest arc across the sign flip") {
    // 270° about Y is the SAME rotation as -90° about Y, but its quaternion has a
    // negative dot with the identity. slerp must flip it and interpolate the short
    // way: the half-way point is -45° about Y (+X -> (cos45, 0, +sin45)), NOT the
    // +135° the long way round would give.
    const Quat far = Quat::fromAxisAngle(Vec3{0, 1, 0}, radians(270.0f));
    const Quat mid = slerp(Quat::identity(), far, 0.5f);
    const float k = 0.70710678f;
    checkVec3(mid.rotate(Vec3{1, 0, 0}), Vec3{k, 0.0f, k});
}
