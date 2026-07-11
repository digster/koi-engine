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

#include <algorithm>  // std::find — locating an item in the sorted pointer list
#include <cstdint>    // std::uint32_t — DrawBatch counts
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

// ----------------------------------------------------------------------------
//  Draw-call batching (Step 24) — sortByMaterialMesh / sortByMesh / coalesceBatches.
//  The helpers only COMPARE the mesh/material pointers (never dereference them), so
//  distinct sentinel addresses stand in for real GPU meshes/materials — no device.
// ----------------------------------------------------------------------------
namespace {
int meshTokenA, meshTokenB, meshTokenC;
const Mesh* const kMeshA = reinterpret_cast<const Mesh*>(&meshTokenA);
const Mesh* const kMeshB = reinterpret_cast<const Mesh*>(&meshTokenB);
const Mesh* const kMeshC = reinterpret_cast<const Mesh*>(&meshTokenC);
int matTokenA, matTokenB;
const Material* const kMatA = reinterpret_cast<const Material*>(&matTokenA);
const Material* const kMatB = reinterpret_cast<const Material*>(&matTokenB);

RenderItem batchItem(const Mesh* mesh, const Material* material) {
    RenderItem it;
    it.mesh     = mesh;
    it.material = material;
    return it;
}

// Find the batch for a given (mesh, material) key, or a null-mesh sentinel if absent.
DrawBatch findBatch(const std::vector<DrawBatch>& batches, const Mesh* mesh,
                    const Material* material) {
    for (const DrawBatch& b : batches) {
        if (b.mesh == mesh && b.material == material) return b;
    }
    return DrawBatch{};
}
}  // namespace

TEST_CASE("coalesceBatches groups a sorted list by (mesh, material)") {
    // A pre-sorted list: two (A,0), then one (B,0) — material changes — then one (B,1)
    // — mesh changes. Three batches, contiguous by first index.
    std::vector<RenderItem> items = {
        batchItem(kMeshA, kMatA), batchItem(kMeshA, kMatA),
        batchItem(kMeshA, kMatB), batchItem(kMeshB, kMatB),
    };
    const std::vector<const RenderItem*> sorted = asPointers(items);

    std::vector<DrawBatch> batches;
    coalesceBatches(sorted, /*byMaterial=*/true, batches);

    REQUIRE(batches.size() == 3);
    CHECK(batches[0].mesh == kMeshA); CHECK(batches[0].material == kMatA);
    CHECK(batches[0].first == 0);     CHECK(batches[0].count == 2);
    CHECK(batches[1].mesh == kMeshA); CHECK(batches[1].material == kMatB);
    CHECK(batches[1].first == 2);     CHECK(batches[1].count == 1);
    CHECK(batches[2].mesh == kMeshB); CHECK(batches[2].first == 3);
    CHECK(batches[2].count == 1);
}

TEST_CASE("coalesceBatches by mesh alone ignores material (the shadow pass)") {
    // Same items, but byMaterial=false: the two mesh-A items batch together even though
    // their materials differ, and the batch carries a null material.
    std::vector<RenderItem> items = {
        batchItem(kMeshA, kMatA), batchItem(kMeshA, kMatB),
        batchItem(kMeshA, kMatA), batchItem(kMeshB, kMatB),
    };
    const std::vector<const RenderItem*> sorted = asPointers(items);

    std::vector<DrawBatch> batches;
    coalesceBatches(sorted, /*byMaterial=*/false, batches);

    REQUIRE(batches.size() == 2);
    CHECK(batches[0].mesh == kMeshA);   CHECK(batches[0].count == 3);
    CHECK(batches[0].material == nullptr);
    CHECK(batches[1].mesh == kMeshB);   CHECK(batches[1].count == 1);
}

TEST_CASE("coalesceBatches handles empty, all-same, and all-distinct inputs") {
    std::vector<DrawBatch> batches;

    coalesceBatches({}, true, batches);
    CHECK(batches.empty());

    std::vector<RenderItem> same = {
        batchItem(kMeshA, kMatA), batchItem(kMeshA, kMatA), batchItem(kMeshA, kMatA)};
    coalesceBatches(asPointers(same), true, batches);
    REQUIRE(batches.size() == 1);
    CHECK(batches[0].count == 3);
    CHECK(batches[0].first == 0);

    std::vector<RenderItem> distinct = {
        batchItem(kMeshA, kMatA), batchItem(kMeshB, kMatA), batchItem(kMeshC, kMatA)};
    coalesceBatches(asPointers(distinct), true, batches);
    REQUIRE(batches.size() == 3);
    for (const DrawBatch& b : batches) CHECK(b.count == 1);
}

TEST_CASE("sortByMaterialMesh makes identical draws adjacent so they coalesce") {
    // Interleaved keys: (A,0),(B,0),(A,0),(B,1). Sorting must gather the two (A,0)s
    // together, so coalescing yields exactly three batches for four items.
    std::vector<RenderItem> items = {
        batchItem(kMeshA, kMatA), batchItem(kMeshA, kMatB),
        batchItem(kMeshA, kMatA), batchItem(kMeshB, kMatB),
    };
    std::vector<const RenderItem*> list = asPointers(items);

    sortByMaterialMesh(list);
    std::vector<DrawBatch> batches;
    coalesceBatches(list, /*byMaterial=*/true, batches);

    CHECK(batches.size() == 3);  // (A,0)x2, (A,1)x1, (B,1)x1 — NOT 4
    const DrawBatch a0 = findBatch(batches, kMeshA, kMatA);
    CHECK(a0.count == 2);
    std::uint32_t total = 0;
    for (const DrawBatch& b : batches) total += b.count;
    CHECK(total == 4);
}

TEST_CASE("sortByMaterialMesh is stable within a key") {
    // Two items share the key (A, matA); a stable sort must keep items[0] before
    // items[2] in the output (their input order), never swap equal-key items.
    std::vector<RenderItem> items = {
        batchItem(kMeshA, kMatA),  // 0
        batchItem(kMeshB, kMatB),  // 1
        batchItem(kMeshA, kMatA),  // 2
    };
    std::vector<const RenderItem*> list = asPointers(items);
    sortByMaterialMesh(list);

    const auto pos0 = std::find(list.begin(), list.end(), &items[0]) - list.begin();
    const auto pos2 = std::find(list.begin(), list.end(), &items[2]) - list.begin();
    CHECK(pos0 < pos2);  // equal-key items keep their relative order
}

TEST_CASE("sortByMesh groups by mesh regardless of material") {
    std::vector<RenderItem> items = {
        batchItem(kMeshA, kMatA), batchItem(kMeshB, kMatB),
        batchItem(kMeshA, kMatB), batchItem(kMeshB, kMatA),
    };
    std::vector<const RenderItem*> list = asPointers(items);

    sortByMesh(list);
    std::vector<DrawBatch> batches;
    coalesceBatches(list, /*byMaterial=*/false, batches);

    REQUIRE(batches.size() == 2);  // one per mesh
    CHECK(findBatch(batches, kMeshA, nullptr).count == 2);
    CHECK(findBatch(batches, kMeshB, nullptr).count == 2);
}
