// ============================================================================
//  DebugDraw.hpp — immediate-mode line drawing for visualizing spatial data
// ----------------------------------------------------------------------------
//  Up to now the engine's spatial machinery has been INVISIBLE. Step 19 gave us
//  bounding boxes and a camera frustum; Step 20 used them to CULL off-screen
//  draws — but you can't SEE an AABB or a frustum, so a wrong bound or a mis-
//  built frustum is a silent bug you can only infer from numbers. Every real
//  engine solves this the same way: a "debug draw" API that renders throwaway
//  LINES on top of the scene (a box around a collider, an arrow for a normal, a
//  wireframe of the camera's frustum). This is that API.
//
//  IMMEDIATE MODE. Nothing here persists between frames. Each frame the app
//  simply RE-DECLARES the lines it wants — `clear()`, then a burst of
//  `line()`/`box()`/`frustum()` calls — and hands the collected vertices to the
//  renderer, which uploads them once and draws them in a single line-list draw.
//  There is no retained list of "debug objects" to keep in sync with the scene;
//  the lines you ask for this frame are exactly the lines you get. That's why
//  the API mutates a plain vertex buffer instead of building objects: it mirrors
//  how DrawLine/DrawBox debug helpers work in production engines and keeps the
//  whole thing a trivial, allocation-reusing collector.
//
//  This file is PURE (no SDL, no GPU): it only turns shapes into a flat list of
//  coloured vertices. That keeps it fully unit-testable (tests/test_debug_draw.cpp)
//  and lets the GPU side (the line pipeline + per-frame upload in GpuRenderer)
//  stay a thin consumer. Concepts are explained in
//  documentation/docs/23-debug-draw.html.
// ============================================================================
#pragma once

#include <span>    // std::span — a (pointer,length) view the renderer uploads
#include <vector>  // std::vector — the growing per-frame vertex list

#include "math/Geometry.hpp"  // Aabb — box() draws a bounding box's 12 edges
#include "math/Mat4.hpp"      // Mat4 — frustum() unprojects through its inverse
#include "math/Vec.hpp"       // Vec3 — positions + colours

namespace koi {

// One vertex of a debug line: a world-space position and a flat RGB colour. The
// GPU line pipeline reads exactly this layout (pos at offset 0, colour at 12),
// so the field order and types are a binding contract with debug_line.vert.
struct DebugVertex {
    Vec3 position;  // world-space endpoint
    Vec3 color;     // flat, unlit colour (0..1 per channel)
};

// Collects the debug lines to draw this frame. Lines are stored as pairs of
// DebugVertex (a LINE LIST: every two vertices are one independent segment), so
// the renderer can draw them all with one SDL_GPU_PRIMITIVETYPE_LINELIST call.
class DebugDraw {
public:
    // Drop last frame's lines. The vector keeps its capacity, so re-filling it
    // each frame costs no allocation once it has grown to its working size.
    void clear() { verts_.clear(); }

    // A single segment from a to b in one colour (two vertices appended).
    void line(const Vec3& a, const Vec3& b, const Vec3& color);

    // The 12 edges of an axis-aligned bounding box (24 vertices). This is how a
    // mesh's world-space bounds become visible: box(mesh.localBounds().transformed(world)).
    void box(const Aabb& box, const Vec3& color);

    // The wireframe of the frustum described by a view-projection matrix. The
    // elegant part: a frustum is just the CLIP-SPACE UNIT CUBE viewed from world
    // space, so we take the cube's 8 corners in NDC (x,y ∈ [-1,1], z ∈ [0,1] —
    // our Metal/Vulkan depth convention) and push them BACKWARD through
    // inverse(viewProj) to recover the world-space corners, then connect the same
    // 12 edges as box(). Feed it (proj*view) to draw the camera's own frustum —
    // exactly the volume Step 20's culler tests against, finally made visible.
    void frustum(const Mat4& viewProj, const Vec3& color);

    // A short segment from `origin` along `dir`, `length` world units long (dir
    // is normalized first). Handy for a surface normal or a light's aim.
    void ray(const Vec3& origin, const Vec3& dir, float length, const Vec3& color);

    // Three axis-aligned segments crossing at `center`, each `size` long in total
    // (half to each side) — a cheap position marker, used for light icons.
    void cross(const Vec3& center, float size, const Vec3& color);

    // A read-only view of the collected vertices for the renderer to upload. Empty
    // when nothing was queued this frame (the renderer then skips the draw).
    [[nodiscard]] std::span<const DebugVertex> vertices() const { return verts_; }
    [[nodiscard]] bool   empty() const { return verts_.empty(); }
    [[nodiscard]] size_t size()  const { return verts_.size(); }

private:
    // Append the 12 edges connecting 8 corners indexed by the bit convention
    // corner[i] = (x from bit0, y from bit1, z from bit2). Shared by box() (world
    // corners from an AABB) and frustum() (world corners unprojected from NDC).
    void addBoxEdges(const Vec3 corners[8], const Vec3& color);

    std::vector<DebugVertex> verts_;
};

}  // namespace koi
