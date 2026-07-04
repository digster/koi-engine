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

// Transpose: mirror across the diagonal — element (r,c) swaps with (c,r). For a
// pure ROTATION matrix this also happens to be its inverse (rotations are
// orthonormal), and it's a building block of the "normal matrix" below.
[[nodiscard]] inline Mat4 transpose(const Mat4& m) {
    Mat4 out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.at(r, c) = m.at(c, r);
        }
    }
    return out;
}

// General 4x4 inverse.
//
//  The inverse "undoes" a matrix: inverse(M) * M == identity. We need it for two
//  jobs that arrive with the geometry utilities (Step 19):
//    * UNPROJECTION — turning a 2D cursor back into a 3D world ray for picking
//      needs the inverse of the view-projection matrix.
//    * THE NORMAL MATRIX — transpose(inverse(model)) is what correctly carries
//      surface normals through a non-uniform scale (see triangle.vert).
//
//  This is the textbook cofactor/adjugate method: build the matrix of cofactors,
//  and divide by the determinant. A NON-obvious but load-bearing detail: this
//  cofactor code indexes the flat array as if it were ROW-major, yet our storage
//  is COLUMN-major. Those two "wrongs" cancel — reading a column-major array
//  row-major is the transpose, and inverse-of-transpose equals transpose-of-
//  inverse, so the result lands back in correct column-major order. (The
//  inverse(M)*M == identity unit test is what actually pins this down.)
//
//  If the matrix is singular (determinant ~ 0, e.g. a zero scale collapsed an
//  axis) there is no inverse; we return identity rather than NaNs — the same
//  "degenerate input returns something sane" stance normalize() takes for the
//  zero vector.
[[nodiscard]] inline Mat4 inverse(const Mat4& mat) {
    const float* m = mat.m;
    float inv[16];

    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] +
               m[9]*m[7]*m[14]  + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] -
               m[8]*m[7]*m[14]  - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] +
               m[8]*m[7]*m[13]  + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] -
               m[8]*m[6]*m[13]  - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] -
               m[9]*m[3]*m[14]  - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] +
               m[8]*m[3]*m[14]  + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] -
               m[8]*m[3]*m[13]  - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] +
               m[8]*m[2]*m[13]  + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] +
               m[5]*m[3]*m[14]  + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] -
               m[4]*m[3]*m[14]  - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] +
               m[4]*m[3]*m[13]  + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] -
               m[4]*m[2]*m[13]  - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] -
               m[5]*m[3]*m[10]  - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] +
               m[4]*m[3]*m[10]  + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] -
               m[4]*m[3]*m[9]   - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] +
               m[4]*m[2]*m[9]   + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    // Determinant, expanded along the first row using the cofactors above.
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::abs(det) < 1e-12f) {
        return Mat4::identity();  // singular: no inverse exists — fail soft.
    }
    const float invDet = 1.0f / det;

    Mat4 out;
    for (int i = 0; i < 16; ++i) {
        out.m[i] = inv[i] * invDet;
    }
    return out;
}

// Translation: moves a point by t. Lives in the last column.
[[nodiscard]] inline Mat4 translation(Vec3 t) {
    Mat4 out = Mat4::identity();
    out.at(0, 3) = t.x;
    out.at(1, 3) = t.y;
    out.at(2, 3) = t.z;
    return out;
}

// Scale: stretches a point's coordinates by s per axis. Unlike translation, a
// scale lives on the diagonal (it multiplies each axis), so a direction (w = 0)
// is scaled too — only translation distinguishes points from directions. The
// bottom-right element stays 1 so the homogeneous w is untouched. Used by
// Transform to give scene-graph nodes a size.
[[nodiscard]] inline Mat4 scaling(Vec3 s) {
    Mat4 out = Mat4::identity();
    out.at(0, 0) = s.x;
    out.at(1, 1) = s.y;
    out.at(2, 2) = s.z;
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

// Orthographic projection (Step 9 — used for the directional light's shadow map).
//
//  Unlike perspective, an orthographic projection has NO foreshortening: parallel
//  lines stay parallel and an object's on-screen size doesn't change with distance.
//  That's exactly right for a DIRECTIONAL light (a "sun"), whose rays are parallel —
//  we render the scene's depth through a box-shaped frustum aligned with the light.
//
//  Same conventions as perspective(): right-handed (looks down -Z) and depth mapped
//  to z ∈ [0, 1] (near plane → 0, far plane → 1), matching SDL3's NDC. The box is
//  [l, r] × [b, t] in x/y and [n, f] in distance along the view's -Z.
[[nodiscard]] inline Mat4 orthographic(float l, float r, float b, float t,
                                       float n, float f) {
    Mat4 out = Mat4::identity();
    out.at(0, 0) = 2.0f / (r - l);
    out.at(1, 1) = 2.0f / (t - b);
    out.at(2, 2) = -1.0f / (f - n);       // view z -n → 0, -f → 1
    out.at(0, 3) = -(r + l) / (r - l);
    out.at(1, 3) = -(t + b) / (t - b);
    out.at(2, 3) = -n / (f - n);
    return out;
}

// Look-at view matrix — the "camera" transform.
//
//  There is no camera in the GPU; the vertex shader only knows clip space. To
//  "place a camera" we instead transform the whole world by the camera's INVERSE:
//  moving the camera right is the same as sliding the world left. lookAt builds
//  exactly that inverse from where the camera is (`eye`), what it points at
//  (`center`), and which way is up (`up`).
//
//  It constructs an orthonormal camera basis — forward `f`, right `s`, true up
//  `u` — and packs it so that multiplying a world point expresses that point in
//  the camera's frame. Right-handed: the camera looks down its own -Z (forward),
//  matching our perspective() convention.
[[nodiscard]] inline Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    const Vec3 f = normalize(center - eye);  // forward: from the eye toward the target
    const Vec3 s = normalize(cross(f, up));  // right:   perpendicular to forward & up
    const Vec3 u = cross(s, f);              // true up: re-orthogonalized

    Mat4 out = Mat4::identity();
    // Rotation part: the camera basis as rows (so it maps world axes → camera axes).
    out.at(0, 0) = s.x;  out.at(0, 1) = s.y;  out.at(0, 2) = s.z;
    out.at(1, 0) = u.x;  out.at(1, 1) = u.y;  out.at(1, 2) = u.z;
    out.at(2, 0) = -f.x; out.at(2, 1) = -f.y; out.at(2, 2) = -f.z;
    // Translation part: shift the world by -eye, expressed in the camera basis.
    out.at(0, 3) = -dot(s, eye);
    out.at(1, 3) = -dot(u, eye);
    out.at(2, 3) =  dot(f, eye);
    return out;
}

}  // namespace koi
