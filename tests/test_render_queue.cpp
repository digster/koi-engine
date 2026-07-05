// ============================================================================
//  test_render_queue.cpp — unit tests for the Step 20 render queue
// ----------------------------------------------------------------------------
//  The two pure halves of the render queue — computeLocalBounds (a mesh's
//  model-space box) and cullToFrustum (the visibility filter) — carry no GPU
//  state, so a headless test pins their behaviour exactly. buildRenderQueue is
//  NOT tested here: it needs real Meshes (which need a GPU device), and its
//  output is verified end-to-end by the byte-stable KOI_CAPTURE frame instead.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include <vector>

#include "math/Geometry.hpp"
#include "math/Mat4.hpp"
#include "renderer/RenderQueue.hpp"
#include "renderer/Vertex.hpp"
#include "scene/Material.hpp"  // partitionByBlend reads Material::alphaMode

using namespace koi;

// Build a Vertex at (x, y, z); the other attributes don't matter for bounds.
static Vertex vertexAt(float x, float y, float z) {
    Vertex v{};
    v.position[0] = x;
    v.position[1] = y;
    v.position[2] = z;
    return v;
}

// A render item that only carries bounds — mesh/material are irrelevant to
// culling, which is exactly why the queue can be exercised without a GPU.
static RenderItem itemWithBounds(const Aabb& bounds) {
    RenderItem item;
    item.worldBounds = bounds;
    return item;
}

// ----------------------------------------------------------------------------
//  computeLocalBounds
// ----------------------------------------------------------------------------
TEST_CASE("computeLocalBounds tightly wraps the vertex positions") {
    // A little cluster whose extremes are, per axis: x∈[-2,3], y∈[-1,4], z∈[0,5].
    const std::vector<Vertex> verts = {
        vertexAt(-2.0f, 4.0f, 0.0f),
        vertexAt(3.0f, -1.0f, 5.0f),
        vertexAt(1.0f, 2.0f, 2.0f),
    };
    const Aabb box = computeLocalBounds(verts);
    CHECK(box.min.x == doctest::Approx(-2.0f));
    CHECK(box.min.y == doctest::Approx(-1.0f));
    CHECK(box.min.z == doctest::Approx(0.0f));
    CHECK(box.max.x == doctest::Approx(3.0f));
    CHECK(box.max.y == doctest::Approx(4.0f));
    CHECK(box.max.z == doctest::Approx(5.0f));
}

TEST_CASE("computeLocalBounds of a single vertex is a degenerate point box") {
    const std::vector<Vertex> verts = { vertexAt(7.0f, 8.0f, 9.0f) };
    const Aabb box = computeLocalBounds(verts);
    CHECK(box.min.x == doctest::Approx(7.0f));
    CHECK(box.max.x == doctest::Approx(7.0f));
    CHECK(box.center().y == doctest::Approx(8.0f));
    CHECK(box.extents().z == doctest::Approx(0.0f));
}

TEST_CASE("computeLocalBounds of no vertices is the empty (inverted) box") {
    // Folding zero points leaves the identity box untouched: min = +inf, max = -inf,
    // which every frustum test rejects and every merge() absorbs.
    const Aabb box = computeLocalBounds(std::span<const Vertex>{});
    CHECK(box.min.x > box.max.x);  // inverted → contains nothing
}

// ----------------------------------------------------------------------------
//  cullToFrustum — the same camera set-up test_geometry.cpp uses for the frustum:
//  eye at z = +5 looking toward the origin, 90° FOV, near 0.1, far 100.
// ----------------------------------------------------------------------------
static Frustum demoFrustum() {
    const Mat4 view = lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Mat4 proj = perspective(radians(90.0f), 1.0f, 0.1f, 100.0f);
    return Frustum::fromViewProjection(proj * view);
}

TEST_CASE("cullToFrustum keeps only the items the camera can see") {
    const Frustum f = demoFrustum();
    std::vector<RenderItem> items = {
        itemWithBounds(Aabb{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}),   // 0: at origin, IN VIEW
        itemWithBounds(Aabb{{-0.5f, -0.5f, 19.5f}, {0.5f, 0.5f, 20.5f}}),  // 1: behind the camera
        itemWithBounds(Aabb{{49.5f, -0.5f, 0.0f}, {50.5f, 0.5f, 1.0f}}),   // 2: far off to the side
    };

    std::vector<const RenderItem*> visible;
    const std::size_t kept = cullToFrustum(items, f, visible);

    CHECK(kept == 1);
    REQUIRE(visible.size() == 1);
    CHECK(visible[0] == &items[0]);  // the in-view item, by pointer identity
}

TEST_CASE("cullToFrustum keeps a straddling box and preserves order") {
    const Frustum f = demoFrustum();
    std::vector<RenderItem> items = {
        itemWithBounds(Aabb{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}),   // 0: in view
        itemWithBounds(Aabb{{-0.5f, -0.5f, 19.5f}, {0.5f, 0.5f, 20.5f}}),  // 1: behind camera (culled)
        itemWithBounds(Aabb{{-100, -100, -1}, {100, 100, 1}}),             // 2: straddles the frustum
    };

    std::vector<const RenderItem*> visible;
    const std::size_t kept = cullToFrustum(items, f, visible);

    CHECK(kept == 2);
    REQUIRE(visible.size() == 2);
    CHECK(visible[0] == &items[0]);  // survivors keep their original relative order
    CHECK(visible[1] == &items[2]);
}

TEST_CASE("cullToFrustum clears the output before filling it") {
    const Frustum f = demoFrustum();
    std::vector<RenderItem> items = {
        itemWithBounds(Aabb{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}),
    };
    // Pre-seed the output with junk; cullToFrustum must not append to it.
    std::vector<const RenderItem*> visible = { nullptr, nullptr, nullptr };
    const std::size_t kept = cullToFrustum(items, f, visible);
    CHECK(kept == 1);
    CHECK(visible.size() == 1);
}

// ----------------------------------------------------------------------------
//  partitionByBlend — the Step 21 opaque/transparent split + back-to-front sort.
//  Pure (Material is header-only, no GPU), so we build real Materials + items and
//  view them through a pointer list exactly as recordScene does after culling.
// ----------------------------------------------------------------------------

// A render item tagged with a material and centred on `center` (a small point box,
// so worldBounds.center() == center drives the distance sort).
static RenderItem itemFor(const Material* mat, Vec3 center) {
    RenderItem item;
    item.material    = mat;
    item.worldBounds = Aabb{center - Vec3{0.1f, 0.1f, 0.1f}, center + Vec3{0.1f, 0.1f, 0.1f}};
    return item;
}

// Wrap a vector<RenderItem> in the pointer list partitionByBlend consumes.
static std::vector<const RenderItem*> asPointers(const std::vector<RenderItem>& items) {
    std::vector<const RenderItem*> ptrs;
    for (const RenderItem& it : items) {
        ptrs.push_back(&it);
    }
    return ptrs;
}

TEST_CASE("partitionByBlend splits items by AlphaMode, opaque in queue order") {
    Material opaque;  // defaults to AlphaMode::Opaque
    Material blend;   blend.alphaMode = AlphaMode::Blend;

    // Interleave the two modes so a correct split can't be an accident of ordering.
    std::vector<RenderItem> items = {
        itemFor(&opaque, {0, 0, 0}),
        itemFor(&blend,  {0, 0, 1}),
        itemFor(&opaque, {0, 0, 2}),
        itemFor(&blend,  {0, 0, 3}),
    };
    const std::vector<const RenderItem*> visible = asPointers(items);

    std::vector<const RenderItem*> opaqueOut, transparentOut;
    partitionByBlend(visible, /*cameraPos=*/{0, 0, 10}, /*sortTransparent=*/true,
                     opaqueOut, transparentOut);

    // Opaque bucket keeps the queue's original relative order.
    REQUIRE(opaqueOut.size() == 2);
    CHECK(opaqueOut[0] == &items[0]);
    CHECK(opaqueOut[1] == &items[2]);
    // Transparent bucket holds exactly the two BLEND items.
    REQUIRE(transparentOut.size() == 2);
}

TEST_CASE("partitionByBlend orders transparent items back-to-front") {
    Material blend;  blend.alphaMode = AlphaMode::Blend;
    // Camera at z = +10 looking toward the origin. In queue order the items are at
    // z = 0, 8, 4 → distances 10, 2, 6. Back-to-front = farthest first = 0, 4, 8.
    std::vector<RenderItem> items = {
        itemFor(&blend, {0, 0, 0}),  // distance 10 (farthest)
        itemFor(&blend, {0, 0, 8}),  // distance 2  (nearest)
        itemFor(&blend, {0, 0, 4}),  // distance 6
    };
    const std::vector<const RenderItem*> visible = asPointers(items);

    std::vector<const RenderItem*> opaqueOut, transparentOut;
    partitionByBlend(visible, /*cameraPos=*/{0, 0, 10}, /*sortTransparent=*/true,
                     opaqueOut, transparentOut);

    CHECK(opaqueOut.empty());
    REQUIRE(transparentOut.size() == 3);
    CHECK(transparentOut[0] == &items[0]);  // z=0, farthest → drawn first
    CHECK(transparentOut[1] == &items[2]);  // z=4
    CHECK(transparentOut[2] == &items[1]);  // z=8, nearest → drawn last (on top)
}

TEST_CASE("partitionByBlend leaves transparent in queue order when sorting is off") {
    Material blend;  blend.alphaMode = AlphaMode::Blend;
    std::vector<RenderItem> items = {
        itemFor(&blend, {0, 0, 0}),
        itemFor(&blend, {0, 0, 8}),
        itemFor(&blend, {0, 0, 4}),
    };
    const std::vector<const RenderItem*> visible = asPointers(items);

    std::vector<const RenderItem*> opaqueOut, transparentOut;
    partitionByBlend(visible, /*cameraPos=*/{0, 0, 10}, /*sortTransparent=*/false,
                     opaqueOut, transparentOut);

    // No sort → the queue's original order is preserved (this is the A/B "wrong" case).
    REQUIRE(transparentOut.size() == 3);
    CHECK(transparentOut[0] == &items[0]);
    CHECK(transparentOut[1] == &items[1]);
    CHECK(transparentOut[2] == &items[2]);
}

TEST_CASE("partitionByBlend clears both outputs before filling them") {
    Material opaque;
    Material blend;  blend.alphaMode = AlphaMode::Blend;
    std::vector<RenderItem> items = { itemFor(&opaque, {0, 0, 0}), itemFor(&blend, {0, 0, 1}) };
    const std::vector<const RenderItem*> visible = asPointers(items);

    // Pre-seed both outputs with junk; partitionByBlend must replace, not append.
    std::vector<const RenderItem*> opaqueOut = { nullptr, nullptr };
    std::vector<const RenderItem*> transparentOut = { nullptr, nullptr, nullptr };
    partitionByBlend(visible, {0, 0, 10}, true, opaqueOut, transparentOut);

    CHECK(opaqueOut.size() == 1);
    CHECK(transparentOut.size() == 1);
}
