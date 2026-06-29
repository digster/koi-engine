// ============================================================================
//  test_math.cpp — unit tests for the hand-rolled vector & matrix math
// ----------------------------------------------------------------------------
//  Math is the ideal thing to unit-test: it's pure (no GPU, no window, no IO),
//  so we can assert exact results on any machine. Because we wrote this math
//  ourselves (no GLM), these tests are also our safety net — a sign error in a
//  rotation or projection matrix is otherwise an infuriating visual bug. We use
//  doctest's Approx for the float comparisons (floating-point math is rarely
//  bit-exact).
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include "math/Mat4.hpp"
#include "math/Vec.hpp"

using namespace koi;

TEST_CASE("Vec3 dot product measures alignment") {
    CHECK(dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == doctest::Approx(32.0f));
    // Perpendicular vectors have a zero dot product.
    CHECK(dot(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == doctest::Approx(0.0f));
}

TEST_CASE("Vec3 cross product is perpendicular to both inputs") {
    // +X cross +Y = +Z (right-hand rule).
    const Vec3 c = cross(Vec3{1, 0, 0}, Vec3{0, 1, 0});
    CHECK(c.x == doctest::Approx(0.0f));
    CHECK(c.y == doctest::Approx(0.0f));
    CHECK(c.z == doctest::Approx(1.0f));
}

TEST_CASE("Vec3 length and normalize") {
    CHECK(length(Vec3{3, 4, 0}) == doctest::Approx(5.0f));   // 3-4-5 triangle
    const Vec3 n = normalize(Vec3{0, 5, 0});
    CHECK(n.x == doctest::Approx(0.0f));
    CHECK(n.y == doctest::Approx(1.0f));
    CHECK(n.z == doctest::Approx(0.0f));
    CHECK(length(n) == doctest::Approx(1.0f));
}

TEST_CASE("Mat4 identity leaves a vector unchanged") {
    const Mat4 I = Mat4::identity();
    const Vec4 v = I * Vec4{7, -3, 2, 1};
    CHECK(v.x == doctest::Approx(7.0f));
    CHECK(v.y == doctest::Approx(-3.0f));
    CHECK(v.z == doctest::Approx(2.0f));
    CHECK(v.w == doctest::Approx(1.0f));
}

TEST_CASE("Mat4 identity is the multiplicative identity") {
    const Mat4 T = translation(Vec3{1, 2, 3});
    const Mat4 IT = Mat4::identity() * T;
    for (int i = 0; i < 16; ++i) {
        CHECK(IT.m[i] == doctest::Approx(T.m[i]));
    }
}

TEST_CASE("translation moves a point (and leaves a direction alone)") {
    const Mat4 T = translation(Vec3{10, 20, 30});
    // A point (w = 1) is translated...
    const Vec4 p = T * Vec4{1, 2, 3, 1};
    CHECK(p.x == doctest::Approx(11.0f));
    CHECK(p.y == doctest::Approx(22.0f));
    CHECK(p.z == doctest::Approx(33.0f));
    // ...a direction (w = 0) is not (translation only affects positions).
    const Vec4 d = T * Vec4{1, 2, 3, 0};
    CHECK(d.x == doctest::Approx(1.0f));
    CHECK(d.y == doctest::Approx(2.0f));
    CHECK(d.z == doctest::Approx(3.0f));
}

TEST_CASE("rotationZ by 90 degrees maps +X to +Y") {
    const Mat4 R = rotationZ(radians(90.0f));
    const Vec4 v = R * Vec4{1, 0, 0, 1};
    CHECK(v.x == doctest::Approx(0.0f));
    CHECK(v.y == doctest::Approx(1.0f));
    CHECK(v.z == doctest::Approx(0.0f));
}

TEST_CASE("perspective maps the near/far planes to z in [0, 1]") {
    constexpr float nearZ = 0.1f;
    constexpr float farZ  = 100.0f;
    const Mat4 P = perspective(radians(60.0f), 16.0f / 9.0f, nearZ, farZ);

    // A point ON the near plane (view z = -near) projects to NDC z = 0.
    const Vec4 nearClip = P * Vec4{0, 0, -nearZ, 1};
    CHECK(nearClip.z / nearClip.w == doctest::Approx(0.0f));

    // A point ON the far plane (view z = -far) projects to NDC z = 1.
    const Vec4 farClip = P * Vec4{0, 0, -farZ, 1};
    CHECK(farClip.z / farClip.w == doctest::Approx(1.0f));
}
