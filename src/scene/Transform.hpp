// ============================================================================
//  Transform.hpp — where a thing is, how it's turned, and how big it is
// ----------------------------------------------------------------------------
//  A Transform is the human-friendly description of an object's placement:
//  position, rotation, and scale (often abbreviated "TRS"). On its own it's just
//  three vectors; localMatrix() bakes them into the single 4x4 matrix the GPU
//  actually consumes (the "model" matrix in the MVP pipeline from documentation/docs/04).
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
//  Rotation is stored as Euler ANGLES (one angle per axis, in RADIANS) and
//  composed Z * Y * X. Euler angles are simple and match our Camera; the project
//  deliberately defers quaternions until a step actually needs them.
//
//  Header-only and SDL-free (pure math), like Vec/Mat4, so it's trivially
//  unit-testable (tests/test_transform.cpp).
// ============================================================================
#pragma once

#include "math/Mat4.hpp"
#include "math/Vec.hpp"

namespace koi {

struct Transform {
    Vec3 position      {0.0f, 0.0f, 0.0f};  // where it sits in its parent's space
    Vec3 rotationEuler {0.0f, 0.0f, 0.0f};  // radians, per axis (x, y, z)
    Vec3 scale         {1.0f, 1.0f, 1.0f};  // 1 = unchanged size

    // Bake TRS into one "local" model matrix (local = relative to the parent;
    // the scene graph turns this into a world matrix by prepending the parent's).
    [[nodiscard]] Mat4 localMatrix() const {
        return translation(position) *
               rotationZ(rotationEuler.z) *
               rotationY(rotationEuler.y) *
               rotationX(rotationEuler.x) *
               scaling(scale);
    }
};

}  // namespace koi
