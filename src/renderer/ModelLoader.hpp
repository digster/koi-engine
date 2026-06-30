// ============================================================================
//  ModelLoader.hpp — load a 3D model from a file into a Mesh
// ----------------------------------------------------------------------------
//  Through Step 8 all geometry was hand-built (the cube + plane in Primitives).
//  Step 9 loads REAL models from disk. Parsing model formats (the separate
//  vertex/uv/normal indexing of OBJ, glTF's JSON + binary accessors) is fiddly
//  bookkeeping with little conceptual payoff — so, unlike our hand-rolled math,
//  we use small single-header LIBRARIES for it: tinyobjloader (.obj) and cgltf
//  (.glb/.gltf). That keeps the from-scratch effort on the graphics (lighting,
//  shadows) where the learning is.
//
//  loadModel reads the file's first mesh — positions, normals, UVs, indices —
//  into our koi::Vertex layout and uploads it via GpuRenderer::createMesh, just
//  like a primitive. Vertex color defaults to white so a material's texture shows
//  unchanged. The engine assigns its own Material (we read geometry only, not the
//  file's materials/textures).
// ============================================================================
#pragma once

#include <memory>

namespace koi {

class Mesh;
class GpuRenderer;

// Load `path` (.obj → tinyobjloader, .glb/.gltf → cgltf) into a new Mesh. Returns
// nullptr (after logging) on an unsupported extension or a load/parse failure.
[[nodiscard]] std::shared_ptr<Mesh> loadModel(GpuRenderer& renderer, const char* path);

}  // namespace koi
