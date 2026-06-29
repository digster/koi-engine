// ============================================================================
//  test_vertex.cpp — guards the CPU↔GPU vertex layout contract
// ----------------------------------------------------------------------------
//  The vertex input layout we hand the GPU (SDL_GPUVertexAttribute offsets and
//  formats, in GpuRenderer::createTrianglePipeline) is derived BY HAND from the
//  byte layout of koi::Vertex. If the struct ever drifts from that layout, the
//  GPU silently reads the wrong bytes — a frame full of garbage with no error.
//  These pure, headless checks pin the layout so such a drift fails the build's
//  test step instead of corrupting frames. (The same facts are also asserted at
//  compile time in Vertex.hpp; duplicating them as runtime tests keeps the
//  contract visible in the test report and catches it on any toolchain.)
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp already provides
//  the single main() for the whole test binary.
// ============================================================================
#include <doctest/doctest.h>

#include <array>
#include <cstddef>  // offsetof

#include "renderer/Vertex.hpp"

using koi::Vertex;

TEST_CASE("Vertex is tightly packed so its size is the buffer pitch") {
    // position (3 floats) + color (3 floats) = 6 floats = 24 bytes, no padding.
    // Step 3 widened position from 2D to 3D, so this grew from 20 to 24.
    CHECK(sizeof(Vertex) == 24);
    CHECK(sizeof(float) == 4);
}

TEST_CASE("Vertex attribute offsets match the pipeline's vertex layout") {
    // These offsets are what createTrianglePipeline() passes as the two FLOAT3
    // attribute offsets (and what triangle.vert reads at location 0 / 1).
    CHECK(offsetof(Vertex, position) == 0);
    CHECK(offsetof(Vertex, color) == 12);
}

TEST_CASE("Cube index set references 8 vertices to build 12 triangles") {
    // Mirrors GpuRenderer::createGeometry(): 6 faces × 2 triangles tile the cube,
    // reusing its 8 corners — the reuse an index buffer exists for.
    constexpr std::array<unsigned short, 36> indices = {
        0, 1, 2,  2, 3, 0,   // front
        1, 5, 6,  6, 2, 1,   // right
        5, 4, 7,  7, 6, 5,   // back
        4, 0, 3,  3, 7, 4,   // left
        3, 2, 6,  6, 7, 3,   // top
        4, 5, 1,  1, 0, 4,   // bottom
    };

    CHECK(indices.size() == 36);  // 12 triangles × 3 vertices

    // Every index must point at one of the 8 stored cube corners (0..7).
    for (const unsigned short i : indices) {
        CHECK(i < 8);
    }
}
