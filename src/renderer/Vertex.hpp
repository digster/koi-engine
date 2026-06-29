// ============================================================================
//  Vertex.hpp — the CPU-side description of one vertex
// ----------------------------------------------------------------------------
//  A "vertex" is one corner of our geometry. Each carries a set of attributes
//  (here: a 2D position and an RGB color). We store many of these contiguously
//  in a std::array / C array and upload that block verbatim into a GPU *vertex
//  buffer* (see GpuRenderer::createMesh and renderer/Primitives.cpp).
//
//  WHY THE EXACT BYTE LAYOUT MATTERS
//  The GPU does not parse C++ types — it reads raw bytes. When we build the
//  graphics pipeline we hand it a *vertex input layout* (SDL_GPUVertexAttribute
//  entries) that says, byte-for-byte, where each attribute lives inside one
//  vertex and what format it is. That layout MUST match this struct exactly:
//
//      field      bytes      shader attribute            SDL format
//      --------   --------   -------------------------   ----------------------
//      position   [0, 12)    layout(location = 0) vec3   VERTEXELEMENTFORMAT_FLOAT3
//      color      [12, 24)   layout(location = 1) vec3   VERTEXELEMENTFORMAT_FLOAT3
//      uv         [24, 32)   layout(location = 2) vec2   VERTEXELEMENTFORMAT_FLOAT2
//      sizeof  == 32 == the vertex buffer "pitch" (stride between vertices)
//
//  Step 3 widened the position from 2D to 3D (z added), so the cube lives in
//  real 3D space; that pushed color's offset from 8 to 12 and the pitch to 24.
//  Step 6 appended a 2D texture coordinate (uv) for sampling a texture; because it
//  goes last, position/color keep their offsets and only the pitch grows to 32.
//
//  We deliberately use plain `float[]` members (not koi::Vec3) so the layout is
//  obvious and trivially standard-layout. The static_asserts below pin the
//  contract so a future change to this struct fails the build (and the unit
//  tests in tests/test_vertex.cpp) instead of silently corrupting frames.
// ============================================================================
#pragma once

#include <cstddef>  // offsetof, size_t

namespace koi {

struct Vertex {
    float position[3];  // x, y, z in model space
    float color[3];     // r, g, b in 0..1 — a tint multiplied with the texture
    float uv[2];        // texture coordinates: (0,0) = top-left, (1,1) = bottom-right
};

// The vertex input layout we describe to the GPU is derived from these facts.
// If any of them changes, update createTrianglePipeline()'s attributes to match.
static_assert(sizeof(Vertex) == 32, "Vertex must be tightly packed (pitch = 32 bytes)");
static_assert(offsetof(Vertex, position) == 0, "position must be the first attribute");
static_assert(offsetof(Vertex, color) == 12, "color must follow the 3-float position");
static_assert(offsetof(Vertex, uv) == 24, "uv must follow the 3-float color");

}  // namespace koi
