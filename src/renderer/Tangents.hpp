// ============================================================================
//  Tangents.hpp — computing a per-vertex TANGENT for normal mapping (Step 13)
// ----------------------------------------------------------------------------
//  WHY A TANGENT EXISTS
//  A normal map stores a surface normal per texel, but NOT in world space — it
//  stores it in "tangent space", a local frame that follows the texture across the
//  surface. To use one, the shader must rotate the sampled normal from tangent
//  space into world space, and that rotation is the TBN matrix built from three
//  world-space axes at each fragment:
//
//      T (tangent)   — the direction along the surface where the texture's U
//                      coordinate increases
//      B (bitangent) — likewise for V (we reconstruct it in the shader as N×T)
//      N (normal)    — the surface normal we already have
//
//  N alone can't tell us which way "U" runs, so T must be derived from how the UVs
//  are laid out over the geometry and stored per vertex. This header holds the pure,
//  SDL-free math for that so it can be unit-tested headlessly (tests/test_tangent.cpp),
//  the same "CPU twin" pattern as Pbr.hpp / Light.hpp. ModelLoader.cpp uses it to fill
//  tangents for loaded meshes; Primitives.cpp hardcodes them for the cube/plane.
//
//  METHOD (Lengyel). For a triangle we solve for the tangent that maps the UV U-axis
//  onto the surface: given the two edge vectors and the matching UV deltas,
//      T = (edge1 · ΔV2 − edge2 · ΔV1) / (ΔU1·ΔV2 − ΔU2·ΔV1).
//  We accumulate this per shared vertex (so adjacent faces average, like smooth
//  normals) and finally re-orthonormalize each tangent against its normal.
//
//  SIMPLIFICATION: we store only a vec3 tangent and let the shader take B = N×T.
//  That assumes a consistent UV handedness; mirrored UVs would need a signed
//  (vec4) tangent whose w flips the bitangent. Fine for our meshes — noted as a
//  known limitation in documentation/docs/14-texture-and-normal-maps.html.
// ============================================================================
#pragma once

#include <cmath>

#include "math/Vec.hpp"

namespace koi {

// The raw (unnormalized) tangent contributed by ONE triangle, from its three
// positions and their three UVs. Returns {0,0,0} for a degenerate UV mapping
// (zero-area in UV space), so the caller can simply skip that contribution
// instead of producing a NaN.
[[nodiscard]] inline Vec3 triangleTangent(const Vec3& p0, const Vec3& p1, const Vec3& p2,
                                          const Vec2& uv0, const Vec2& uv1, const Vec2& uv2) {
    const Vec3 edge1 = p1 - p0;
    const Vec3 edge2 = p2 - p0;
    const float du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
    const float du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;

    const float det = du1 * dv2 - du2 * dv1;  // area of the UV triangle (×2)
    if (std::fabs(det) < 1e-12f) {
        return Vec3{0.0f, 0.0f, 0.0f};        // degenerate UVs — no usable direction
    }
    const float r = 1.0f / det;
    return (edge1 * dv2 - edge2 * dv1) * r;    // unnormalized; magnitude ∝ contribution
}

// Any unit vector perpendicular to `n` — the fallback when a vertex ends up with no
// usable tangent (all-degenerate UVs, or a tangent that collapsed onto the normal).
// We cross `n` with whichever world axis it is least aligned with, so the cross is
// never near-zero.
[[nodiscard]] inline Vec3 anyPerpendicular(const Vec3& n) {
    const Vec3 axis = (std::fabs(n.x) < 0.9f) ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    return normalize(cross(n, axis));
}

// Gram-Schmidt: project the accumulated tangent onto the plane perpendicular to the
// (already-normalized) normal, then normalize — giving a clean orthonormal T for the
// TBN basis. Falls back to an arbitrary perpendicular if the tangent is unusable, so
// the result is always a valid unit vector orthogonal to `normal`.
[[nodiscard]] inline Vec3 orthonormalizeTangent(const Vec3& tangent, const Vec3& normal) {
    const Vec3 t = tangent - normal * dot(normal, tangent);
    const float len = length(t);
    if (len < 1e-6f) {
        return anyPerpendicular(normal);
    }
    return t * (1.0f / len);
}

}  // namespace koi
