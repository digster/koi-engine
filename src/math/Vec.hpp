// ============================================================================
//  Vec.hpp — small hand-rolled vector types (Vec2 / Vec3 / Vec4)
// ----------------------------------------------------------------------------
//  This is the engine's first piece of real math. The project deliberately
//  AVOIDS a library like GLM: writing the vectors ourselves means nothing about
//  how 3D works stays a black box. These types are intentionally tiny — plain
//  structs of floats with free functions for the operations the renderer and
//  the matrix code actually need.
//
//  WHY HEADER-ONLY: the functions are short, pure, and `constexpr`-friendly, so
//  defining them inline in the header keeps all the math in one readable place
//  and lets the unit tests (tests/test_math.cpp) use them without linking. This
//  is a deliberate exception to the engine's usual .hpp/.cpp split.
//
//  A quick refresher on the geometry, since the docs are a beginner's guide:
//    * A vector is a direction + magnitude (or just a point's coordinates).
//    * DOT product measures how aligned two vectors are (and gives cos of the
//      angle between unit vectors); 0 means perpendicular.
//    * CROSS product (Vec3 only) returns a vector perpendicular to both inputs —
//      the basis of surface normals and "which way is up/right".
//    * NORMALIZing scales a vector to length 1 so it encodes pure direction.
// ============================================================================
#pragma once

#include <cmath>  // std::sqrt

namespace koi {

// ---------------------------------------------------------------------------
//  Vec2 — a 2D vector / point.
// ---------------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

[[nodiscard]] constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] constexpr Vec2 operator*(Vec2 v, float s) { return {v.x * s, v.y * s}; }

// ---------------------------------------------------------------------------
//  Vec3 — a 3D vector / point. The workhorse: positions, directions, colors.
// ---------------------------------------------------------------------------
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
[[nodiscard]] constexpr Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
[[nodiscard]] constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }

// Dot product: a.x*b.x + a.y*b.y + a.z*b.z. Scalar measure of alignment.
[[nodiscard]] constexpr float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Cross product: a vector perpendicular to both a and b (right-hand rule).
[[nodiscard]] constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

// Length (magnitude) = sqrt(dot(v, v)). Not constexpr: std::sqrt isn't until C++26.
[[nodiscard]] inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

// Return v scaled to length 1 (its pure direction). Guards against the zero
// vector, which has no direction, by returning it unchanged.
[[nodiscard]] inline Vec3 normalize(Vec3 v) {
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}

// ---------------------------------------------------------------------------
//  Vec4 — a 4D vector. In 3D graphics this is a Vec3 plus a "w" component, the
//  homogeneous coordinate that makes translation and perspective expressible as
//  a single 4x4 matrix multiply (see Mat4.hpp). Points use w = 1, directions
//  w = 0.
// ---------------------------------------------------------------------------
struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

[[nodiscard]] constexpr Vec4 operator+(Vec4 a, Vec4 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
[[nodiscard]] constexpr Vec4 operator*(Vec4 v, float s) {
    return {v.x * s, v.y * s, v.z * s, v.w * s};
}

}  // namespace koi
