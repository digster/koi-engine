#include "renderer/Primitives.hpp"

#include <array>  // std::array for the compile-time geometry tables

#include "renderer/GpuRenderer.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Vertex.hpp"

namespace koi {

std::shared_ptr<Mesh> makeCubeMesh(GpuRenderer& renderer) {
    // The cube's 8 unique corners, centered on the origin (±0.5 on each axis) in
    // MODEL space. Each corner's color is its position shifted into 0..1 — so the
    // cube is literally the RGB color cube, and every corner is a distinct color
    // (the (-,-,-) corner is black, (+,+,+) is white). The depth test makes it
    // read as solid and three-dimensional.
    //
    //        7───────6        Corner index = bits (x,y,z), - = -0.5, + = +0.5:
    //       /│      /│          0:(-,-,-) 1:(+,-,-) 2:(+,+,-) 3:(-,+,-)
    //      4───────5 │          4:(-,-,+) 5:(+,-,+) 6:(+,+,+) 7:(-,+,+)
    //      │ 3─────│─2
    //      │/      │/
    //      0───────1
    constexpr float n = -0.5f, p = 0.5f;
    constexpr std::array<Vertex, 8> vertices = {{
        { { n, n, n }, { 0.0f, 0.0f, 0.0f } },  // 0
        { { p, n, n }, { 1.0f, 0.0f, 0.0f } },  // 1
        { { p, p, n }, { 1.0f, 1.0f, 0.0f } },  // 2
        { { n, p, n }, { 0.0f, 1.0f, 0.0f } },  // 3
        { { n, n, p }, { 0.0f, 0.0f, 1.0f } },  // 4
        { { p, n, p }, { 1.0f, 0.0f, 1.0f } },  // 5
        { { p, p, p }, { 1.0f, 1.0f, 1.0f } },  // 6
        { { n, p, p }, { 0.0f, 1.0f, 1.0f } },  // 7
    }};

    // 12 triangles (2 per face) referencing the 8 corners — 36 indices reusing
    // 8 vertices, the index buffer's payoff. Each pair below is one cube face.
    constexpr std::array<Uint16, 36> indices = {
        0, 1, 2,  2, 3, 0,   // front  (z = -0.5)
        1, 5, 6,  6, 2, 1,   // right  (x = +0.5)
        5, 4, 7,  7, 6, 5,   // back   (z = +0.5)
        4, 0, 3,  3, 7, 4,   // left   (x = -0.5)
        3, 2, 6,  6, 7, 3,   // top    (y = +0.5)
        4, 5, 1,  1, 0, 4,   // bottom (y = -0.5)
    };

    return renderer.createMesh(vertices, indices);
}

std::shared_ptr<Mesh> makePlaneMesh(GpuRenderer& renderer) {
    // A big flat quad lying in the XZ plane at y = 0. Only two triangles, sharing
    // their middle edge via the index buffer (4 vertices, 6 indices). A single
    // muted slate color so it reads as a calm floor rather than competing with the
    // vivid cubes. (The Engine drops this node a couple of units below the cubes.)
    constexpr float h = 6.0f;                         // half-extent: spans [-6, +6]
    constexpr float r = 0.20f, g = 0.22f, b = 0.26f;  // muted cool gray
    constexpr std::array<Vertex, 4> vertices = {{
        { { -h, 0.0f, -h }, { r, g, b } },  // 0: back-left
        { {  h, 0.0f, -h }, { r, g, b } },  // 1: back-right
        { {  h, 0.0f,  h }, { r, g, b } },  // 2: front-right
        { { -h, 0.0f,  h }, { r, g, b } },  // 3: front-left
    }};
    constexpr std::array<Uint16, 6> indices = {
        0, 1, 2,  2, 3, 0,
    };

    return renderer.createMesh(vertices, indices);
}

}  // namespace koi
