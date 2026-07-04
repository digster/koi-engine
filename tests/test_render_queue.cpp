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
