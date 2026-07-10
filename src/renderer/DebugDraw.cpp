// ============================================================================
//  DebugDraw.cpp — turn shapes into coloured line-list vertices (see the header)
// ============================================================================
#include "renderer/DebugDraw.hpp"

namespace koi {

// The 12 edges of a cube, as index pairs into an 8-corner array whose corners
// follow the bit convention corner[i] = (x=bit0, y=bit1, z=bit2). Each edge
// connects two corners that differ in exactly ONE axis bit: 4 edges along X
// (bit0 flips), 4 along Y (bit1), 4 along Z (bit2). Shared by box + frustum.
namespace {
constexpr int kBoxEdges[12][2] = {
    {0, 1}, {2, 3}, {4, 5}, {6, 7},  // along X (differ in bit0)
    {0, 2}, {1, 3}, {4, 6}, {5, 7},  // along Y (differ in bit1)
    {0, 4}, {1, 5}, {2, 6}, {3, 7},  // along Z (differ in bit2)
};
}  // namespace

void DebugDraw::line(const Vec3& a, const Vec3& b, const Vec3& color) {
    verts_.push_back(DebugVertex{a, color});
    verts_.push_back(DebugVertex{b, color});
}

void DebugDraw::addBoxEdges(const Vec3 corners[8], const Vec3& color) {
    for (const auto& e : kBoxEdges) {
        line(corners[e[0]], corners[e[1]], color);
    }
}

void DebugDraw::box(const Aabb& box, const Vec3& color) {
    // Enumerate the 8 corners by picking min or max on each axis per the bit
    // convention (bit0→x, bit1→y, bit2→z), then draw the 12 connecting edges.
    Vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        corners[i] = Vec3{
            (i & 1) ? box.max.x : box.min.x,
            (i & 2) ? box.max.y : box.min.y,
            (i & 4) ? box.max.z : box.min.z,
        };
    }
    addBoxEdges(corners, color);
}

void DebugDraw::frustum(const Mat4& viewProj, const Vec3& color) {
    // A frustum in world space is the clip-space UNIT CUBE seen through the
    // camera. inverse(viewProj) is the map from clip space back to world space,
    // so unprojecting the cube's 8 NDC corners gives the frustum's world corners.
    const Mat4 inv = inverse(viewProj);

    Vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        // NDC cube corner: x,y ∈ {-1,+1}; z ∈ {0,1} — Koi's Metal/Vulkan depth
        // range (near plane at z=0, far at z=1), NOT OpenGL's [-1,1]. The bit
        // convention matches box(): bit0→x, bit1→y, bit2→z (0=near, 1=far).
        const Vec4 ndc{
            (i & 1) ? 1.0f : -1.0f,
            (i & 2) ? 1.0f : -1.0f,
            (i & 4) ? 1.0f :  0.0f,
            1.0f,
        };
        // Unproject and complete the perspective divide: a point at the far plane
        // has clip.w ≫ 1, so dividing by w is what actually spreads the corners
        // out into the truncated pyramid we see.
        const Vec4 world = inv * ndc;
        const float invW = (world.w != 0.0f) ? 1.0f / world.w : 0.0f;
        corners[i] = Vec3{world.x * invW, world.y * invW, world.z * invW};
    }
    addBoxEdges(corners, color);
}

void DebugDraw::ray(const Vec3& origin, const Vec3& dir, float length, const Vec3& color) {
    line(origin, origin + normalize(dir) * length, color);
}

void DebugDraw::cross(const Vec3& center, float size, const Vec3& color) {
    const float h = size * 0.5f;  // half-length to each side
    line(center - Vec3{h, 0, 0}, center + Vec3{h, 0, 0}, color);
    line(center - Vec3{0, h, 0}, center + Vec3{0, h, 0}, color);
    line(center - Vec3{0, 0, h}, center + Vec3{0, 0, h}, color);
}

}  // namespace koi
