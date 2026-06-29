// ============================================================================
//  Mat4.hpp — a hand-rolled 4x4 matrix, the heart of 3D transforms
// ----------------------------------------------------------------------------
//  A 4x4 matrix is how we move, rotate, and project geometry. Multiplying a
//  vertex position by the right matrix relocates it; multiplying matrices
//  together composes their effects into one. The whole "MVP" pipeline
//  (Model * View * Projection) is just three of these multiplied in order.
//
//  COLUMN-MAJOR STORAGE — and why it matters.
//  GLSL (and therefore the SPIR-V/MSL our shaders compile to) interprets a
//  `mat4` uniform as COLUMN-MAJOR: the first 4 floats are the first COLUMN, not
//  the first row. We store our matrix the same way so we can memcpy it straight
//  into the uniform buffer with no transpose. Element at (row r, col c) lives at
//  flat index `c*4 + r`:
//
//        col0 col1 col2 col3
//   row0 [ 0   4    8   12 ]
//   row1 [ 1   5    9   13 ]
//   row2 [ 2   6   10   14 ]
//   row3 [ 3   7   11   15 ]
//
//  Translations therefore live in the last column (indices 12,13,14).
//
//  Header-only for the same reasons as Vec.hpp: small, pure, easy to test.
// ============================================================================
#pragma once

#include <cmath>  // std::sin, std::cos, std::tan

#include "math/Vec.hpp"

namespace koi {

// Convenience: degrees -> radians (all trig here works in radians).
inline constexpr float kPi = 3.14159265358979323846f;
[[nodiscard]] inline constexpr float radians(float degrees) { return degrees * (kPi / 180.0f); }

struct Mat4 {
    // Column-major; default-constructed to all zeros. Use Mat4::identity() for
    // the multiplicative identity.
    float m[16] = {};

    // Read/write element (row r, col c) with the column-major mapping above.
    [[nodiscard]] float& at(int r, int c) { return m[c * 4 + r]; }
    [[nodiscard]] float  at(int r, int c) const { return m[c * 4 + r]; }

    // The identity matrix: leaves any vector unchanged when multiplied.
    [[nodiscard]] static Mat4 identity() {
        Mat4 out;
        out.m[0] = out.m[5] = out.m[10] = out.m[15] = 1.0f;
        return out;
    }
};

// Matrix * matrix. Result(r,c) = sum_k A(r,k) * B(k,c). Reads right-to-left
// when applied to a vector: (A*B)*v applies B first, then A.
[[nodiscard]] inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 out;  // zero-initialized
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.at(r, k) * b.at(k, c);
            }
            out.at(r, c) = sum;
        }
    }
    return out;
}

// Matrix * vector. result[r] = sum_c M(r,c) * v[c].
[[nodiscard]] inline Vec4 operator*(const Mat4& mat, Vec4 v) {
    return {
        mat.at(0, 0) * v.x + mat.at(0, 1) * v.y + mat.at(0, 2) * v.z + mat.at(0, 3) * v.w,
        mat.at(1, 0) * v.x + mat.at(1, 1) * v.y + mat.at(1, 2) * v.z + mat.at(1, 3) * v.w,
        mat.at(2, 0) * v.x + mat.at(2, 1) * v.y + mat.at(2, 2) * v.z + mat.at(2, 3) * v.w,
        mat.at(3, 0) * v.x + mat.at(3, 1) * v.y + mat.at(3, 2) * v.z + mat.at(3, 3) * v.w,
    };
}

// Translation: moves a point by t. Lives in the last column.
[[nodiscard]] inline Mat4 translation(Vec3 t) {
    Mat4 out = Mat4::identity();
    out.at(0, 3) = t.x;
    out.at(1, 3) = t.y;
    out.at(2, 3) = t.z;
    return out;
}

// Rotation about the X axis (spins the Y/Z plane), angle in radians.
[[nodiscard]] inline Mat4 rotationX(float radiansAngle) {
    const float c = std::cos(radiansAngle);
    const float s = std::sin(radiansAngle);
    Mat4 out = Mat4::identity();
    out.at(1, 1) = c;  out.at(1, 2) = -s;
    out.at(2, 1) = s;  out.at(2, 2) =  c;
    return out;
}

// Rotation about the Y axis (spins the X/Z plane), angle in radians.
[[nodiscard]] inline Mat4 rotationY(float radiansAngle) {
    const float c = std::cos(radiansAngle);
    const float s = std::sin(radiansAngle);
    Mat4 out = Mat4::identity();
    out.at(0, 0) =  c;  out.at(0, 2) = s;
    out.at(2, 0) = -s;  out.at(2, 2) = c;
    return out;
}

// Rotation about the Z axis (spins the X/Y plane), angle in radians.
// A +90° rotation maps +X to +Y (counter-clockwise looking down -Z).
[[nodiscard]] inline Mat4 rotationZ(float radiansAngle) {
    const float c = std::cos(radiansAngle);
    const float s = std::sin(radiansAngle);
    Mat4 out = Mat4::identity();
    out.at(0, 0) = c;  out.at(0, 1) = -s;
    out.at(1, 0) = s;  out.at(1, 1) =  c;
    return out;
}

// Perspective projection.
//
//  Turns the camera's view space into clip space, giving farther objects a
//  smaller on-screen size (the w-divide the GPU does after the vertex shader is
//  what creates the foreshortening). This matrix is RIGHT-HANDED (the camera
//  looks down -Z; smaller/more-negative z is farther away) and maps depth to
//  z ∈ [0, 1] — the convention SDL3's GPU API uses (Metal/Vulkan/D3D), NOT
//  OpenGL's [-1, 1]. +Y stays up, matching SDL3's NDC.
//
//    fovYRadians : vertical field of view (how "wide" the lens is)
//    aspect      : viewport width / height (corrects the Step 2 stretch)
//    nearZ, farZ : the visible depth range (both positive distances)
[[nodiscard]] inline Mat4 perspective(float fovYRadians, float aspect,
                                      float nearZ, float farZ) {
    const float tanHalf = std::tan(fovYRadians * 0.5f);
    Mat4 out;  // zero-initialized
    out.at(0, 0) = 1.0f / (aspect * tanHalf);
    out.at(1, 1) = 1.0f / tanHalf;
    out.at(2, 2) = farZ / (nearZ - farZ);
    out.at(2, 3) = -(farZ * nearZ) / (farZ - nearZ);
    out.at(3, 2) = -1.0f;  // copies -z into w, so the GPU divides x/y/z by depth
    return out;
}

}  // namespace koi
