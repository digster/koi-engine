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
    // position (3) + color (3) + uv (2) + normal (3) + tangent (3) = 14 floats = 56
    // bytes. Step 3 widened position to 3D (→24); Step 6 appended uv (→32); Step 7
    // appended normal (→44); Step 13 appended tangent (→56). Each addition went LAST,
    // so earlier offsets held.
    CHECK(sizeof(Vertex) == 56);
    CHECK(sizeof(float) == 4);
}

TEST_CASE("Vertex attribute offsets match the pipeline's vertex layout") {
    // These offsets are what createTrianglePipeline() passes as the five attribute
    // offsets (and what triangle.vert reads at location 0 / 1 / 2 / 3 / 4).
    CHECK(offsetof(Vertex, position) == 0);
    CHECK(offsetof(Vertex, color) == 12);
    CHECK(offsetof(Vertex, uv) == 24);
    CHECK(offsetof(Vertex, normal) == 32);
    CHECK(offsetof(Vertex, tangent) == 44);
}

TEST_CASE("Cube index set references 24 vertices to build 12 triangles") {
    // Mirrors makeCubeMesh(): since Step 6 the cube is 24 vertices (6 faces × 4
    // unshared corners, so each face can carry its own UVs), and each face's 4
    // consecutive vertices form 2 triangles: {4f, 4f+1, 4f+2, 4f+2, 4f+3, 4f}.
    constexpr std::array<unsigned short, 36> indices = {
         0,  1,  2,   2,  3,  0,   // front
         4,  5,  6,   6,  7,  4,   // right
         8,  9, 10,  10, 11,  8,   // back
        12, 13, 14,  14, 15, 12,   // left
        16, 17, 18,  18, 19, 16,   // top
        20, 21, 22,  22, 23, 20,   // bottom
    };

    CHECK(indices.size() == 36);  // 12 triangles × 3 vertices

    // Every index must point at one of the 24 stored cube vertices (0..23).
    for (const unsigned short i : indices) {
        CHECK(i < 24);
    }
}
