#include "renderer/Primitives.hpp"

#include <array>  // std::array for the compile-time geometry tables

#include "renderer/GpuRenderer.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Vertex.hpp"

namespace koi {

std::shared_ptr<Mesh> makeCubeMesh(GpuRenderer& renderer) {
    // A cube has 8 corners, but it needs 24 VERTICES once it carries per-face data.
    // A texture coordinate — and a surface NORMAL — belongs to a (corner, face)
    // pair, not to a corner alone: the top-front-right corner faces three different
    // ways on the three faces it touches. A vertex holds one uv and one normal, so
    // each of the 6 faces gets its own 4 unshared vertices (6 × 4 = 24).
    //
    // Each vertex carries: position; the Step-3 color (corner shifted into 0..1, the
    // RGB cube) used as a tint; a per-face uv covering the whole texture; and the
    // face's flat NORMAL — the same outward direction for all 4 of a face's corners,
    // which is what gives the cube crisp, flat-shaded faces.
    constexpr float n = -0.5f, p = 0.5f;
    constexpr std::array<Vertex, 24> vertices = {{
        // front  (z = -0.5), normal -Z
        { { n, n, n }, { 0, 0, 0 }, { 0, 1 }, { 0, 0, -1 } },
        { { p, n, n }, { 1, 0, 0 }, { 1, 1 }, { 0, 0, -1 } },
        { { p, p, n }, { 1, 1, 0 }, { 1, 0 }, { 0, 0, -1 } },
        { { n, p, n }, { 0, 1, 0 }, { 0, 0 }, { 0, 0, -1 } },
        // right  (x = +0.5), normal +X
        { { p, n, n }, { 1, 0, 0 }, { 0, 1 }, { 1, 0, 0 } },
        { { p, n, p }, { 1, 0, 1 }, { 1, 1 }, { 1, 0, 0 } },
        { { p, p, p }, { 1, 1, 1 }, { 1, 0 }, { 1, 0, 0 } },
        { { p, p, n }, { 1, 1, 0 }, { 0, 0 }, { 1, 0, 0 } },
        // back   (z = +0.5), normal +Z
        { { p, n, p }, { 1, 0, 1 }, { 0, 1 }, { 0, 0, 1 } },
        { { n, n, p }, { 0, 0, 1 }, { 1, 1 }, { 0, 0, 1 } },
        { { n, p, p }, { 0, 1, 1 }, { 1, 0 }, { 0, 0, 1 } },
        { { p, p, p }, { 1, 1, 1 }, { 0, 0 }, { 0, 0, 1 } },
        // left   (x = -0.5), normal -X
        { { n, n, p }, { 0, 0, 1 }, { 0, 1 }, { -1, 0, 0 } },
        { { n, n, n }, { 0, 0, 0 }, { 1, 1 }, { -1, 0, 0 } },
        { { n, p, n }, { 0, 1, 0 }, { 1, 0 }, { -1, 0, 0 } },
        { { n, p, p }, { 0, 1, 1 }, { 0, 0 }, { -1, 0, 0 } },
        // top    (y = +0.5), normal +Y
        { { n, p, n }, { 0, 1, 0 }, { 0, 1 }, { 0, 1, 0 } },
        { { p, p, n }, { 1, 1, 0 }, { 1, 1 }, { 0, 1, 0 } },
        { { p, p, p }, { 1, 1, 1 }, { 1, 0 }, { 0, 1, 0 } },
        { { n, p, p }, { 0, 1, 1 }, { 0, 0 }, { 0, 1, 0 } },
        // bottom (y = -0.5), normal -Y
        { { n, n, p }, { 0, 0, 1 }, { 0, 1 }, { 0, -1, 0 } },
        { { p, n, p }, { 1, 0, 1 }, { 1, 1 }, { 0, -1, 0 } },
        { { p, n, n }, { 1, 0, 0 }, { 1, 0 }, { 0, -1, 0 } },
        { { n, n, n }, { 0, 0, 0 }, { 0, 0 }, { 0, -1, 0 } },
    }};

    // 12 triangles (2 per face). Each face's 4 vertices are consecutive, so face f
    // uses indices {4f, 4f+1, 4f+2, 4f+2, 4f+3, 4f}.
    constexpr std::array<Uint16, 36> indices = {
         0,  1,  2,   2,  3,  0,   // front
         4,  5,  6,   6,  7,  4,   // right
         8,  9, 10,  10, 11,  8,   // back
        12, 13, 14,  14, 15, 12,   // left
        16, 17, 18,  18, 19, 16,   // top
        20, 21, 22,  22, 23, 20,   // bottom
    };

    return renderer.createMesh(vertices, indices);
}

std::shared_ptr<Mesh> makePlaneMesh(GpuRenderer& renderer) {
    // A big flat quad in the XZ plane at y = 0 (4 vertices, 6 indices). Its UVs run
    // 0..kTiles instead of 0..1, so with the sampler's REPEAT address mode the
    // texture TILES kTiles×kTiles across the floor instead of stretching once. A
    // soft blue-gray tint keeps the floor distinct from the vivid cubes. Lying flat,
    // every corner's normal points straight up (+Y).
    constexpr float h = 6.0f;                          // half-extent: spans [-6, +6]
    constexpr float kTiles = 6.0f;                     // texture repeats 6× per axis
    constexpr float r = 0.60f, g = 0.64f, b = 0.72f;   // soft blue-gray tint
    constexpr std::array<Vertex, 4> vertices = {{
        { { -h, 0.0f, -h }, { r, g, b }, { 0.0f,   0.0f   }, { 0, 1, 0 } },  // 0: back-left
        { {  h, 0.0f, -h }, { r, g, b }, { kTiles, 0.0f   }, { 0, 1, 0 } },  // 1: back-right
        { {  h, 0.0f,  h }, { r, g, b }, { kTiles, kTiles }, { 0, 1, 0 } },  // 2: front-right
        { { -h, 0.0f,  h }, { r, g, b }, { 0.0f,   kTiles }, { 0, 1, 0 } },  // 3: front-left
    }};
    constexpr std::array<Uint16, 6> indices = {
        0, 1, 2,  2, 3, 0,
    };

    return renderer.createMesh(vertices, indices);
}

}  // namespace koi
