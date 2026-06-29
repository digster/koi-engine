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
    // position (2 floats) + color (3 floats) = 5 floats = 20 bytes, no padding.
    CHECK(sizeof(Vertex) == 20);
    CHECK(sizeof(float) == 4);
}

TEST_CASE("Vertex attribute offsets match the pipeline's vertex layout") {
    // These offsets are what createTrianglePipeline() passes as the FLOAT2 /
    // FLOAT3 attribute offsets (and what triangle.vert reads at location 0 / 1).
    CHECK(offsetof(Vertex, position) == 0);
    CHECK(offsetof(Vertex, color) == 8);
}

TEST_CASE("Quad index set references 4 vertices to build 2 triangles") {
    // Mirrors GpuRenderer::createGeometry(): two triangles tile the quad, with
    // corners 0 and 2 each reused once — the reuse an index buffer exists for.
    constexpr std::array<unsigned short, 6> indices = { 0, 1, 2, 2, 3, 0 };

    CHECK(indices.size() == 6);  // 2 triangles × 3 vertices

    // Every index must point at one of the 4 stored vertices (0..3).
    for (const unsigned short i : indices) {
        CHECK(i < 4);
    }
}
