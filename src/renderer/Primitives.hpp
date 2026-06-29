// ============================================================================
//  Primitives.hpp — built-in geometry factories (cube, ground plane)
// ----------------------------------------------------------------------------
//  Small helpers that define the vertex/index data for a basic shape and upload
//  it via GpuRenderer::createMesh, returning a ready-to-use Mesh. They live
//  outside GpuRenderer so the renderer stays geometry-agnostic: the engine asks
//  for "a cube" or "a plane" and gets a Mesh it can share across many nodes.
//
//  Both shapes use the existing Vertex format (position + color); no shader or
//  pipeline change is needed. Richer primitives (with normals/UVs) arrive when
//  Step 6 adds lighting and textures.
// ============================================================================
#pragma once

#include <memory>

namespace koi {

class Mesh;
class GpuRenderer;

// A unit cube centered on the origin (±0.5 per axis), colored as the RGB color
// cube (each corner's color is its position). Returns nullptr if the upload
// fails (the renderer logs why).
[[nodiscard]] std::shared_ptr<Mesh> makeCubeMesh(GpuRenderer& renderer);

// A flat square in the XZ plane at y = 0 (spanning ±6), a single muted color —
// a simple "ground" so the animated cubes have a floor to sit above. Returns
// nullptr if the upload fails.
[[nodiscard]] std::shared_ptr<Mesh> makePlaneMesh(GpuRenderer& renderer);

}  // namespace koi
