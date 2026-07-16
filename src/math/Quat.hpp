// ============================================================================
//  Quat.hpp — a hand-rolled unit quaternion for rotations
// ----------------------------------------------------------------------------
//  Up to now the engine turned things with Euler angles (an X, a Y, and a Z
//  angle, baked as rotationZ * rotationY * rotationX). That works, but it has
//  two famous problems:
//
//    1. GIMBAL LOCK. Because the three angles are applied in a fixed order, a
//       90° middle rotation can swing one axis directly onto another, and you
//       silently lose a degree of freedom — two of your three knobs now do the
//       same thing. (This is the same failure our Camera dodges by *clamping*
//       pitch just short of ±90°.)
//    2. NO CLEAN INTERPOLATION. To animate from one orientation to another you
//       want to walk the *shortest arc* at a *constant angular speed*. Blending
//       Euler triples component-by-component does neither: it wobbles and can
//       take the long way around.
//
//  A QUATERNION fixes both. Think of it as the 3D generalisation of using a
//  unit complex number e^{iθ} to rotate the 2D plane. A quaternion has one real
//  part (w) and three imaginary parts (x, y, z):  q = w + xi + yj + zk. A UNIT
//  quaternion (length 1) encodes a rotation directly as an axis + angle:
//
//        q = ( sin(θ/2)·axis ,  cos(θ/2) )        // (x,y,z) , w
//
//  The half-angle looks odd but is the whole trick: rotating a vector uses q
//  *twice* (the "sandwich" q·v·q⁻¹ below), so each application contributes θ/2
//  and they sum to θ. Because there is no preferred axis order, there is no
//  gimbal lock; and because unit quaternions live on the surface of a 4D sphere,
//  interpolating between two of them (slerp) is just sliding along that sphere.
//
//  STORAGE ORDER: (x, y, z, w) — the vector part first, scalar last. This is the
//  order glTF stores node rotations in, so when we later import a node hierarchy
//  the data drops straight in with no shuffling.
//
//  Header-only and pure (no SDL), like Vec.hpp / Mat4.hpp, so it is trivially
//  unit-testable (tests/test_quat.cpp). Concepts explained in the tutorial at
//  docs/tuts/19-quaternions.html.
// ============================================================================
#pragma once

#include <cmath>  // std::sin, std::cos, std::sqrt, std::acos

#include "math/Mat4.hpp"  // Quat -> Mat4 conversion (Mat4 stays unaware of Quat)
#include "math/Vec.hpp"

namespace koi {

struct Quat {
    // Default-constructed to the IDENTITY rotation (rotate by nothing): the
    // vector part is zero and the scalar part is 1 (cos(0/2) = 1).
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    // The do-nothing rotation. Same as a default Quat, but explicit at call sites.
    [[nodiscard]] static Quat identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

    // Build a rotation of `radians` about a (possibly non-unit) `axis`.
    //   w = cos(θ/2),  (x,y,z) = sin(θ/2)·axiŝ
    // We normalise the axis so the result is always a UNIT quaternion (a pure
    // rotation with no accidental scaling).
    [[nodiscard]] static Quat fromAxisAngle(Vec3 axis, float radiansAngle) {
        const Vec3  a    = normalize(axis);
        const float half = radiansAngle * 0.5f;
        const float s    = std::sin(half);
        return {a.x * s, a.y * s, a.z * s, std::cos(half)};
    }

    // Convenience/migration helper: build the SAME rotation the old Euler path
    // produced. Transform used to compose rotationZ * rotationY * rotationX, so
    // we compose the equivalent quaternions in the same order (qz applied last,
    // qx first — quaternion multiply reads right-to-left just like matrices).
    // This is what keeps existing call sites behaving identically after the swap.
    // Defined out-of-line below, once the quaternion operator* is in scope.
    [[nodiscard]] static Quat fromEuler(Vec3 radiansXYZ);

    // Recover the rotation from a pure-rotation matrix — the INVERSE of toMat4().
    // Step 25 needs this to import glTF nodes whose transform is given as a raw 4x4
    // matrix (rather than explicit translation/rotation/scale): Transform::fromMatrix
    // strips off translation + scale and hands the leftover orthonormal 3x3 here.
    //
    // We use the trace-based method: naively w = ½√(1+trace) loses all precision
    // near a 180° turn (where the trace → -1 and that square root → 0), so we pick
    // whichever of x/y/z/w is provably largest and solve for it first, then the
    // rest. Reads only the upper-left 3x3 via at(r,c). Defined out-of-line below.
    [[nodiscard]] static Quat fromRotationMatrix(const Mat4& m);

    // The conjugate negates the vector part. For a UNIT quaternion this is also
    // its inverse (the opposite rotation), which is why rotate() below can use it.
    [[nodiscard]] constexpr Quat conjugate() const { return {-x, -y, -z, w}; }

    // The true multiplicative inverse: conjugate / |q|². For a unit quaternion
    // |q|² = 1 so this equals the conjugate; we divide anyway so the function is
    // correct even if the quaternion has drifted off unit length.
    [[nodiscard]] Quat inverse() const {
        const float n2 = x * x + y * y + z * z + w * w;
        const float inv = n2 > 0.0f ? 1.0f / n2 : 0.0f;
        return {-x * inv, -y * inv, -z * inv, w * inv};
    }

    // Rotate a vector by this (unit) quaternion. Mathematically this is the
    // "sandwich" product q·(0,v)·q⁻¹; the closed form below (Euler–Rodrigues)
    // is the same thing with the algebra multiplied out, so it costs a handful
    // of dot/cross products instead of two full quaternion multiplies.
    [[nodiscard]] Vec3 rotate(Vec3 v) const {
        const Vec3 u{x, y, z};          // vector (imaginary) part
        const float s = w;              // scalar (real) part
        return u * (2.0f * dot(u, v)) +
               v * (s * s - dot(u, u)) +
               cross(u, v) * (2.0f * s);
    }

    // Convert to the 4×4 model matrix the GPU consumes. This is the standard
    // quaternion→rotation-matrix formula, written straight into our column-major
    // Mat4 via at(r,c). For a unit quaternion it is exactly a rotation matrix, so
    // it slots into Transform in place of rotationZ*rotationY*rotationX.
    [[nodiscard]] Mat4 toMat4() const {
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;

        Mat4 out = Mat4::identity();
        out.at(0, 0) = 1.0f - 2.0f * (yy + zz);
        out.at(0, 1) =        2.0f * (xy - wz);
        out.at(0, 2) =        2.0f * (xz + wy);
        out.at(1, 0) =        2.0f * (xy + wz);
        out.at(1, 1) = 1.0f - 2.0f * (xx + zz);
        out.at(1, 2) =        2.0f * (yz - wx);
        out.at(2, 0) =        2.0f * (xz - wy);
        out.at(2, 1) =        2.0f * (yz + wx);
        out.at(2, 2) = 1.0f - 2.0f * (xx + yy);
        return out;
    }
};

// Quaternion dot product — treats the four components as a 4D vector. Its sign
// tells us whether two rotations are on the "same side" of the 4D sphere (used
// by slerp to always pick the shorter arc).
[[nodiscard]] constexpr float dot(Quat a, Quat b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

[[nodiscard]] inline float length(Quat q) { return std::sqrt(dot(q, q)); }

// Renormalise to unit length. Repeated multiplication accumulates tiny floating
// -point error that slowly stretches a quaternion off the unit sphere; calling
// this occasionally keeps it a clean rotation.
[[nodiscard]] inline Quat normalize(Quat q) {
    const float len = length(q);
    if (len <= 0.0f) return Quat::identity();
    const float inv = 1.0f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

// Component-wise helpers — mainly the raw material for slerp's nlerp fallback.
[[nodiscard]] constexpr Quat operator-(Quat q) { return {-q.x, -q.y, -q.z, -q.w}; }
[[nodiscard]] constexpr Quat operator+(Quat a, Quat b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
[[nodiscard]] constexpr Quat operator-(Quat a, Quat b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
[[nodiscard]] constexpr Quat operator*(Quat q, float s) {
    return {q.x * s, q.y * s, q.z * s, q.w * s};
}

// HAMILTON PRODUCT — composition of rotations. Like matrices, (a*b) applies b
// first, then a. In (scalar, vector) form with s = w and v = (x,y,z):
//   result.w = sa*sb - dot(va, vb)
//   result.v = sa*vb + sb*va + cross(va, vb)
// Written out per component below.
[[nodiscard]] constexpr Quat operator*(Quat a, Quat b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,  // x
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,  // y
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,  // z
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,  // w
    };
}

// Out-of-line so it can use the quaternion operator* declared just above.
inline Quat Quat::fromEuler(Vec3 radiansXYZ) {
    const Quat qx = fromAxisAngle({1.0f, 0.0f, 0.0f}, radiansXYZ.x);
    const Quat qy = fromAxisAngle({0.0f, 1.0f, 0.0f}, radiansXYZ.y);
    const Quat qz = fromAxisAngle({0.0f, 0.0f, 1.0f}, radiansXYZ.z);
    return qz * qy * qx;
}

// Out-of-line so it can use normalize() (declared above) to shave off any drift
// left by a not-quite-orthonormal input. The four branches mirror the four terms
// of toMat4(): the trace branch solves for w, and each axis branch solves for the
// one component whose squared term dominates the diagonal, so we never divide by a
// vanishing quantity. Sign choices are pinned to toMat4() by test_quat.cpp.
inline Quat Quat::fromRotationMatrix(const Mat4& m) {
    const float m00 = m.at(0, 0), m11 = m.at(1, 1), m22 = m.at(2, 2);
    const float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        // w is the largest component: 4w² = 1 + trace. Solve w, then x/y/z from the
        // off-diagonal DIFFERENCES (e.g. m21 - m12 = 4wx).
        const float s = 0.5f / std::sqrt(trace + 1.0f);  // = 1/(4w)
        q.w = 0.25f / s;
        q.x = (m.at(2, 1) - m.at(1, 2)) * s;
        q.y = (m.at(0, 2) - m.at(2, 0)) * s;
        q.z = (m.at(1, 0) - m.at(0, 1)) * s;
    } else if (m00 > m11 && m00 > m22) {
        // x dominates: 4x² = 1 + m00 - m11 - m22. Off-diagonal SUMS give y/z; the
        // difference m21 - m12 gives w.
        const float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);  // = 4x
        q.x = 0.25f * s;
        q.y = (m.at(0, 1) + m.at(1, 0)) / s;
        q.z = (m.at(0, 2) + m.at(2, 0)) / s;
        q.w = (m.at(2, 1) - m.at(1, 2)) / s;
    } else if (m11 > m22) {
        const float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);  // = 4y
        q.x = (m.at(0, 1) + m.at(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (m.at(1, 2) + m.at(2, 1)) / s;
        q.w = (m.at(0, 2) - m.at(2, 0)) / s;
    } else {
        const float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);  // = 4z
        q.x = (m.at(0, 2) + m.at(2, 0)) / s;
        q.y = (m.at(1, 2) + m.at(2, 1)) / s;
        q.z = 0.25f * s;
        q.w = (m.at(1, 0) - m.at(0, 1)) / s;
    }
    return normalize(q);
}

// SLERP — spherical linear interpolation, the reason quaternions matter for
// animation. It walks the shortest great-circle arc from `a` to `b` on the unit
// 4-sphere at constant angular speed, so t = 0 gives a, t = 1 gives b, and t =
// 0.5 is the true half-way orientation (no wobble, no long way around).
[[nodiscard]] inline Quat slerp(Quat a, Quat b, float t) {
    float cosTheta = dot(a, b);

    // q and -q are the SAME rotation but opposite points on the sphere. If the
    // dot is negative, flip b so we interpolate across the short side (< 180°).
    if (cosTheta < 0.0f) {
        b = -b;
        cosTheta = -cosTheta;
    }

    // Nearly parallel: sin(θ) → 0 makes the general formula divide by ~0. Fall
    // back to a straight (normalised) lerp, which is indistinguishable here.
    constexpr float kParallel = 0.9995f;
    if (cosTheta > kParallel) {
        return normalize(a + (b - a) * t);  // straight lerp, then back onto the sphere
    }

    const float theta    = std::acos(cosTheta);
    const float sinTheta = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / sinTheta;
    const float wb = std::sin(t * theta) / sinTheta;
    return a * wa + b * wb;
}

}  // namespace koi
