// ============================================================================
//  ModelLoader.hpp — load a 3D model (geometry + material) from a file
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
//  unchanged.
//
//  Step 16: for glTF we ALSO import the file's PBR MATERIAL — its base-colour,
//  metallic-roughness, normal, occlusion and emissive maps (decoded from the
//  embedded/external PNGs via stb_image) plus the scalar factors — into a
//  koi::Material. So loadModel returns geometry AND appearance together (a
//  LoadedModel), the natural "a model is a mesh + a material" shape. OBJ still
//  returns geometry only (material == nullptr); the caller supplies its own.
// ============================================================================
#pragma once

#include <memory>

namespace koi {

class Mesh;
struct Material;
class GpuRenderer;

// A model loaded from a file: its geometry, plus the material imported from the
// file when the format carries one. `mesh == nullptr` means the load failed (the
// cause is already logged). `material` is populated for glTF and null for OBJ (we
// import OBJ geometry only — .mtl material import is out of scope), so a caller can
// use the file's material when present and otherwise assign its own.
struct LoadedModel {
    std::shared_ptr<Mesh>     mesh;
    std::shared_ptr<Material> material;
};

// Load `path` (.obj → tinyobjloader, .glb/.gltf → cgltf). Returns a LoadedModel with
// a null mesh (after logging) on an unsupported extension or a load/parse failure.
[[nodiscard]] LoadedModel loadModel(GpuRenderer& renderer, const char* path);

}  // namespace koi
