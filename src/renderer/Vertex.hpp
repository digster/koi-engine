// ============================================================================
//  Vertex.hpp — the CPU-side description of one vertex
// ----------------------------------------------------------------------------
//  A "vertex" is one corner of our geometry. Each carries a set of attributes
//  (here: a 2D position and an RGB color). We store many of these contiguously
//  in a std::array / C array and upload that block verbatim into a GPU *vertex
//  buffer* (see GpuRenderer::createGeometry).
//
//  WHY THE EXACT BYTE LAYOUT MATTERS
//  The GPU does not parse C++ types — it reads raw bytes. When we build the
//  graphics pipeline we hand it a *vertex input layout* (SDL_GPUVertexAttribute
//  entries) that says, byte-for-byte, where each attribute lives inside one
//  vertex and what format it is. That layout MUST match this struct exactly:
//
//      field      bytes      shader attribute            SDL format
//      --------   --------   -------------------------   ----------------------
//      position   [0, 8)     layout(location = 0) vec2   VERTEXELEMENTFORMAT_FLOAT2
//      color      [8, 20)    layout(location = 1) vec3   VERTEXELEMENTFORMAT_FLOAT3
//      sizeof  == 20 == the vertex buffer "pitch" (stride between vertices)
//
//  We deliberately use plain `float[]` members (not a hand-rolled vec2/vec3)
//  because our own math types don't arrive until Step 3; plain floats keep the
//  layout obvious and trivially standard-layout. The static_asserts below pin
//  the contract so a future change to this struct fails the build (and the
//  unit tests in tests/test_vertex.cpp) instead of silently corrupting frames.
// ============================================================================
#pragma once

#include <cstddef>  // offsetof, size_t

namespace koi {

struct Vertex {
    float position[2];  // x, y in Normalized Device Coordinates (-1..+1)
    float color[3];     // r, g, b in 0..1
};

// The vertex input layout we describe to the GPU is derived from these facts.
// If any of them changes, update createTrianglePipeline()'s attributes to match.
static_assert(sizeof(Vertex) == 20, "Vertex must be tightly packed (pitch = 20 bytes)");
static_assert(offsetof(Vertex, position) == 0, "position must be the first attribute");
static_assert(offsetof(Vertex, color) == 8, "color must follow the 2-float position");

}  // namespace koi
