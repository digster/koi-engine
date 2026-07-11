// ============================================================================
//  Geometry.hpp — bounding volumes, rays, planes, and the view frustum
// ----------------------------------------------------------------------------
//  These are the small spatial primitives an engine reaches for again and again.
//  They are deliberately grouped in one header (like Vec2/3/4 share Vec.hpp)
//  because they're really one idea reused three ways:
//
//    * AABB  + FRUSTUM  → CULLING: skip drawing whatever the camera can't see.
//    * RAY   + AABB     → PICKING: find the object under the mouse cursor.
//    * AABB  sweep      → PHYSICS: the cheap "broadphase" that rejects most
//                                  collision pairs before any exact test.
//
//  Everything here is pure math (no SDL, no GPU), header-only, and unit-tested
//  (tests/test_geometry.cpp) — the same recipe as Vec/Mat4/Quat. The concepts
//  are explained from first principles in docs/tuts/20-geometry-utilities.html.
// ============================================================================
#pragma once

#include <algorithm>  // std::min / std::max
#include <cmath>      // std::abs, std::sqrt (via Vec)
#include <limits>     // std::numeric_limits for the "empty" AABB

#include "math/Mat4.hpp"
#include "math/Vec.hpp"

namespace koi {

// ---------------------------------------------------------------------------
//  Ray — a starting point and a direction, i.e. the half-line origin + t*dir
//  for t >= 0. The archetype: the line from the camera THROUGH a mouse-cursor
//  pixel, which we intersect with scene geometry to see what was clicked.
// ---------------------------------------------------------------------------
struct Ray {
    Vec3 origin {0.0f, 0.0f, 0.0f};
    Vec3 dir    {0.0f, 0.0f, -1.0f};  // need not be unit length; slab test copes

    // The point a distance `t` along the ray. t is in units of |dir|, so if dir
    // is normalized, t is an actual world-space distance.
    [[nodiscard]] Vec3 pointAt(float t) const { return origin + dir * t; }
};

// ---------------------------------------------------------------------------
//  Aabb — an Axis-Aligned Bounding Box: the smallest box, with faces parallel
//  to the world axes, that fully contains some geometry. "Axis-aligned" is the
//  whole point — because the faces never tilt, containment and overlap tests
//  collapse to a handful of min/max comparisons per axis (no rotation math).
//  That cheapness is why culling, physics broadphase, and BVH trees all lean on
//  AABBs even though a tighter oriented box would cull a little more.
// ---------------------------------------------------------------------------
struct Aabb {
    Vec3 min {0.0f, 0.0f, 0.0f};
    Vec3 max {0.0f, 0.0f, 0.0f};

    // An intentionally INVERTED box (min = +inf, max = -inf) that contains
    // nothing. It's the identity element for expand()/merge(): start here, fold
    // in points, and you get their tight bounding box. (A default {0,0,0}..{0,0,0}
    // box would wrongly include the origin.)
    [[nodiscard]] static Aabb empty() {
        constexpr float inf = std::numeric_limits<float>::infinity();
        return { { inf,  inf,  inf}, {-inf, -inf, -inf} };
    }

    [[nodiscard]] Vec3 center()  const { return (min + max) * 0.5f; }
    [[nodiscard]] Vec3 extents() const { return (max - min) * 0.5f; }  // half-size

    [[nodiscard]] bool contains(Vec3 p) const {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }

    // Grow the box just enough to include point p (componentwise min/max).
    void expand(Vec3 p) {
        min = { std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z) };
        max = { std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z) };
    }

    // Grow the box to include another box (their union's bounds).
    void merge(const Aabb& other) {
        expand(other.min);
        expand(other.max);
    }

    // Transform this box by a matrix and return a NEW axis-aligned box that
    // bounds the (possibly rotated) result. A rotated box is no longer axis-
    // aligned, so we can't just transform min/max — we transform all EIGHT
    // corners and re-fit a fresh AABB around them. This is how a mesh's local
    // bounds become world-space bounds for culling: aabb.transformed(worldMatrix).
    [[nodiscard]] Aabb transformed(const Mat4& m) const {
        Aabb out = Aabb::empty();
        for (int i = 0; i < 8; ++i) {
            // Pick each corner by choosing min or max on each axis (bit i encodes
            // the three choices).
            const Vec3 corner {
                (i & 1) ? max.x : min.x,
                (i & 2) ? max.y : min.y,
                (i & 4) ? max.z : min.z,
            };
            const Vec4 w = m * Vec4{corner.x, corner.y, corner.z, 1.0f};
            out.expand(Vec3{w.x, w.y, w.z});
        }
        return out;
    }
};

// ---------------------------------------------------------------------------
//  Plane — an infinite flat surface, stored as normal·p + d = 0. The sign of
//  normal·p + d tells you which SIDE of the plane a point is on (and, if the
//  normal is unit length, its magnitude is the distance to the plane). A frustum
//  is just six of these facing inward, so this signed-distance test is the atom
//  of all visibility culling.
// ---------------------------------------------------------------------------
struct Plane {
    Vec3  normal {0.0f, 1.0f, 0.0f};
    float d      {0.0f};

    // > 0 : p is on the side the normal points to.  < 0 : the other side.
    [[nodiscard]] float signedDistance(Vec3 p) const { return dot(normal, p) + d; }

    // Rescale so the normal is unit length (distances become true distances).
    // Extracted frustum planes come out un-normalized, so we fix them up.
    [[nodiscard]] Plane normalized() const {
        const float len = length(normal);
        if (len <= 0.0f) return *this;
        const float inv = 1.0f / len;
        return { normal * inv, d * inv };
    }
};

// ---------------------------------------------------------------------------
//  Frustum — the truncated pyramid of space the camera can actually see: six
//  planes (left, right, bottom, top, near, far) all facing INWARD, so a point
//  is visible exactly when it's on the positive side of all six.
//
//  The elegant trick (Gribb & Hartmann): the six planes fall straight out of the
//  rows of the view-projection matrix, because a clip coordinate IS a row dotted
//  with the point. A point survives clipping when, for its clip.w,
//      -w <= x <= w,   -w <= y <= w,   and (depth range) <= z <= w.
//  Rearranging each inequality to ">= 0" gives a plane = (row3 ± rowK).
//
//  DEPTH CONVENTION MATTERS. The classic formulas assume OpenGL's z in [-1, 1],
//  where the near plane is (row3 + row2). Koi (like Vulkan / D3D / Metal, and our
//  perspective()/orthographic()) maps depth to z in [0, 1], so the near-plane
//  inequality is simply z >= 0  →  the near plane is row2 ALONE, not row3 + row2.
//  Getting this wrong makes objects wink out at the near plane — a nasty, subtle
//  bug. The far plane (z <= w) is (row3 - row2) either way.
// ---------------------------------------------------------------------------
struct Frustum {
    // Order: left, right, bottom, top, near, far.
    Plane planes[6];

    // Build the six inward-facing planes from a view-projection matrix
    // (projection * view). Feed it (proj * view) to cull in world space, or
    // (proj * view * model) to cull in an object's local space.
    [[nodiscard]] static Frustum fromViewProjection(const Mat4& m) {
        // rowK = (m(K,0), m(K,1), m(K,2), m(K,3)). We keep the (a,b,c) as the
        // plane normal and d as the constant, then normalize.
        auto makePlane = [](float a, float b, float c, float d) {
            return Plane{ Vec3{a, b, c}, d }.normalized();
        };
        const auto R = [&m](int r, int c) { return m.at(r, c); };

        Frustum f;
        // Left:   x >= -w  →  row3 + row0
        f.planes[0] = makePlane(R(3,0)+R(0,0), R(3,1)+R(0,1), R(3,2)+R(0,2), R(3,3)+R(0,3));
        // Right:  x <=  w  →  row3 - row0
        f.planes[1] = makePlane(R(3,0)-R(0,0), R(3,1)-R(0,1), R(3,2)-R(0,2), R(3,3)-R(0,3));
        // Bottom: y >= -w  →  row3 + row1
        f.planes[2] = makePlane(R(3,0)+R(1,0), R(3,1)+R(1,1), R(3,2)+R(1,2), R(3,3)+R(1,3));
        // Top:    y <=  w  →  row3 - row1
        f.planes[3] = makePlane(R(3,0)-R(1,0), R(3,1)-R(1,1), R(3,2)-R(1,2), R(3,3)-R(1,3));
        // Near:   z >=  0  →  row2   (the [0,1]-depth difference; NOT row3 + row2)
        f.planes[4] = makePlane(R(2,0), R(2,1), R(2,2), R(2,3));
        // Far:    z <=  w  →  row3 - row2
        f.planes[5] = makePlane(R(3,0)-R(2,0), R(3,1)-R(2,1), R(3,2)-R(2,2), R(3,3)-R(2,3));
        return f;
    }

    // Conservative "is this box at least partially visible?" test. For each
    // plane we check the AABB's POSITIVE VERTEX — the single corner furthest in
    // the plane's normal direction. If even that corner is behind the plane, the
    // whole box is, so we can reject immediately. If every plane's positive
    // vertex is in front, the box is inside or straddling (a rare false-positive
    // in a frustum corner is acceptable — culling only needs to never wrongly
    // reject something visible).
    [[nodiscard]] bool intersectsAabb(const Aabb& box) const {
        for (const Plane& p : planes) {
            const Vec3 positive {
                p.normal.x >= 0.0f ? box.max.x : box.min.x,
                p.normal.y >= 0.0f ? box.max.y : box.min.y,
                p.normal.z >= 0.0f ? box.max.z : box.min.z,
            };
            if (p.signedDistance(positive) < 0.0f) {
                return false;  // fully outside this plane → outside the frustum
            }
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
//  Ray vs AABB — the "slab" method. An AABB is the intersection of three slabs
//  (the space between its two parallel faces on each axis). For each axis we
//  find the pair of t values where the ray crosses that slab's faces, then keep
//  the running OVERLAP of all three intervals. If the overlap ever becomes empty
//  the ray misses; otherwise its start (tmin) is the entry distance.
//
//  We only accept hits with t >= 0 (the ray is a half-line from `origin`), so a
//  box entirely behind the origin correctly reports a miss. If the origin is
//  INSIDE the box, tmin clamps to 0 (you're already touching it).
//
//  On success, tHit receives the entry distance and the function returns true.
//  IEEE infinities make the axis-parallel case (dir component == 0) fall out for
//  free: an infinite t range that the min/max simply ignores when the origin is
//  within the slab, or collapses to a miss when it isn't.
// ---------------------------------------------------------------------------
[[nodiscard]] inline bool intersect(const Ray& ray, const Aabb& box, float& tHit) {
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::infinity();

    // One slab per axis, written out (Vec3 has no operator[]).
    auto slab = [&](float o, float d, float lo, float hi) {
        const float invD = 1.0f / d;
        float t0 = (lo - o) * invD;
        float t1 = (hi - o) * invD;
        if (invD < 0.0f) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
    };
    slab(ray.origin.x, ray.dir.x, box.min.x, box.max.x);
    slab(ray.origin.y, ray.dir.y, box.min.y, box.max.y);
    slab(ray.origin.z, ray.dir.z, box.min.z, box.max.z);

    if (tmax < tmin) return false;  // the three intervals don't all overlap
    tHit = tmin;
    return true;
}

}  // namespace koi
