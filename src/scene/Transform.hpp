// ============================================================================
//  Transform.hpp — where a thing is, how it's turned, and how big it is
// ----------------------------------------------------------------------------
//  A Transform is the human-friendly description of an object's placement:
//  position, rotation, and scale (often abbreviated "TRS"). On its own it's just
//  three vectors; localMatrix() bakes them into the single 4x4 matrix the GPU
//  actually consumes (the "model" matrix in the MVP pipeline from docs/tuts/04).
//
//  WHY ORDER MATTERS — scale, then rotate, then translate.
//  Matrix multiplication is applied right-to-left to a point, so the matrix we
//  build, translation * rotation * scaling, transforms a vertex like this:
//
//        translate( rotate( scale( vertex ) ) )
//
//  That order is the intuitive one:
//    1. SCALE first, around the object's own origin, so a 2x cube grows in place.
//    2. ROTATE the scaled shape, still about its own origin.
//    3. TRANSLATE last, moving the finished shape to its spot in the world.
//  Do it in any other order and you get surprises — e.g. translating before
//  rotating swings the object around the world origin instead of spinning it.
//
//  Rotation is stored as a unit QUATERNION (Step 18). A quaternion has no
//  preferred axis order, so it cannot gimbal-lock the way the old Euler triple
//  could, and two of them can be blended along the shortest arc with slerp —
//  the property skeletal animation will rely on. Build one from the friendlier
//  axis-angle or Euler forms via Quat::fromAxisAngle / Quat::fromEuler. See
//  docs/tuts/19-quaternions.html.
//
//  Header-only and SDL-free (pure math), like Vec/Mat4, so it's trivially
//  unit-testable (tests/test_transform.cpp).
// ============================================================================
#pragma once

#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "math/Vec.hpp"

namespace koi {

struct Transform {
    Vec3 position {0.0f, 0.0f, 0.0f};      // where it sits in its parent's space
    Quat rotation {Quat::identity()};      // orientation (identity = unrotated)
    Vec3 scale    {1.0f, 1.0f, 1.0f};      // 1 = unchanged size

    // Bake TRS into one "local" model matrix (local = relative to the parent;
    // the scene graph turns this into a world matrix by prepending the parent's).
    [[nodiscard]] Mat4 localMatrix() const {
        return translation(position) *
               rotation.toMat4() *
               scaling(scale);
    }

    // Recover a TRS from a baked model matrix — the inverse of localMatrix().
    //
    // Step 25 needs this because glTF may give a node's placement as a raw 4x4
    // MATRIX instead of separate translation/rotation/scale, yet a Transform stores
    // TRS. The glTF spec forbids shear/skew in node matrices, so the matrix is
    // always a clean translation·rotation·scale and this decomposition is exact:
    //   * TRANSLATION is the last column (indices 12,13,14).
    //   * SCALE is the length of each basis column (rotation columns are unit-length,
    //     so any remaining length IS the scale on that axis).
    //   * ROTATION is what's left once each column is divided by its scale — an
    //     orthonormal 3x3 we hand to Quat::fromRotationMatrix.
    // A MIRRORED node (negative determinant, e.g. a left-hand copy) can't be a pure
    // rotation·scale with all-positive scale, so we fold the flip into scale.x; that
    // leaves a proper (det +1) rotation the quaternion path can represent.
    [[nodiscard]] static Transform fromMatrix(const Mat4& m) {
        Transform t;
        t.position = {m.at(0, 3), m.at(1, 3), m.at(2, 3)};

        const Vec3 c0{m.at(0, 0), m.at(1, 0), m.at(2, 0)};  // basis column 0
        const Vec3 c1{m.at(0, 1), m.at(1, 1), m.at(2, 1)};  // basis column 1
        const Vec3 c2{m.at(0, 2), m.at(1, 2), m.at(2, 2)};  // basis column 2
        float sx = length(c0), sy = length(c1), sz = length(c2);
        // Negative determinant → mirrored: absorb the sign into one axis' scale so
        // the leftover basis is a right-handed rotation.
        if (dot(cross(c0, c1), c2) < 0.0f) { sx = -sx; }
        t.scale = {sx, sy, sz};

        // Divide out the scale to get the pure-rotation basis (guarding a zero axis,
        // which would otherwise divide by zero and produce NaNs).
        Mat4 rot = Mat4::identity();
        if (sx != 0.0f) { rot.at(0, 0) = c0.x / sx; rot.at(1, 0) = c0.y / sx; rot.at(2, 0) = c0.z / sx; }
        if (sy != 0.0f) { rot.at(0, 1) = c1.x / sy; rot.at(1, 1) = c1.y / sy; rot.at(2, 1) = c1.z / sy; }
        if (sz != 0.0f) { rot.at(0, 2) = c2.x / sz; rot.at(1, 2) = c2.y / sz; rot.at(2, 2) = c2.z / sz; }
        t.rotation = Quat::fromRotationMatrix(rot);
        return t;
    }
};

}  // namespace koi
