// ============================================================================
//  RenderQueue.hpp — the flat, cullable draw list (Step 20)
// ----------------------------------------------------------------------------
//  Through Step 19 the renderer DREW WHILE IT WALKED: GpuRenderer::recordScene
//  recursed the scene graph and issued a draw call inline at every node. That
//  couples two very different jobs — deciding *what* to draw (a tree walk) and
//  *how* to submit it (GPU commands) — into one pass.
//
//  THE PIVOT: traverse → build a flat list → submit.
//  A "render queue" is that flat list. We walk the tree ONCE, flattening every
//  visible node into a plain std::vector<RenderItem>, and only then loop the list
//  to submit draws. Pulling the two apart is the shared prerequisite of a whole
//  family of features that all operate ON THE LIST rather than the tree:
//    * frustum culling  — drop items the camera can't see (this step's payoff),
//    * material/pipeline sorting — reorder items to cut state changes,
//    * instancing — collapse identical items into one draw,
//    * transparency sorting & deferred shading — later steps.
//  None of those are expressible while drawing is entangled with traversal.
//
//  WHAT'S IN HERE
//    * RenderItem        — one unit of drawable work (mesh + material + placement
//                          + a precomputed world-space bounding box).
//    * computeLocalBounds— a mesh's model-space AABB, folded from its vertices.
//    * buildRenderQueue  — the tree walk that produces the flat list.
//    * cullToFrustum     — the visibility filter (reuses Step 19's Frustum test).
//  computeLocalBounds and cullToFrustum are PURE (no GPU), so tests/ exercises
//  them headlessly; buildRenderQueue needs the full Node/Mesh types and lives in
//  RenderQueue.cpp.
// ============================================================================
#pragma once

#include <cstddef>  // size_t
#include <cstdint>  // uint32_t — DrawBatch instance indices
#include <span>
#include <vector>

#include "math/Geometry.hpp"   // Aabb, Frustum — the Step 19 spatial primitives
#include "math/Mat4.hpp"
#include "renderer/Vertex.hpp"  // computeLocalBounds reads Vertex::position

namespace koi {

class Mesh;       // full definition in renderer/Mesh.hpp
struct Material;  // full definition in scene/Material.hpp
class Node;       // full definition in scene/Node.hpp

// One unit of drawable work: a `mesh` painted with a `material`, placed by its
// `world` matrix, with `worldBounds` — the mesh's local AABB already transformed
// into world space — cached so culling is a cheap plane test with no per-frame
// box rebuild. Non-owning pointers: the scene owns the mesh/material; a RenderItem
// only lives for the frame it's drawn in.
struct RenderItem {
    const Mesh*     mesh     = nullptr;
    const Material* material = nullptr;
    Mat4            world;
    Aabb            worldBounds;
};

// A mesh's LOCAL-space (model-space) bounding box: fold every vertex position
// into an initially-empty box. Pure — no GPU — so it runs at mesh-upload time on
// the CPU-side vertices and is unit-testable. An empty span yields Aabb::empty()
// (the inverted identity box), which transforms and tests as "nothing".
[[nodiscard]] inline Aabb computeLocalBounds(std::span<const Vertex> vertices) {
    Aabb box = Aabb::empty();
    for (const Vertex& v : vertices) {
        box.expand(Vec3{v.position[0], v.position[1], v.position[2]});
    }
    return box;
}

// Walk the scene graph rooted at `root` and append a RenderItem for every node
// that has BOTH a mesh and a material (a drawable). Each item's `worldBounds` is
// the mesh's local box transformed by the node's cached world matrix, so
// Node::updateWorldTransforms() MUST have run this frame first. Group/pivot nodes
// (no mesh) add no item of their own — their transform is already folded into
// their descendants' world matrices. Defined in RenderQueue.cpp (needs Node/Mesh).
void buildRenderQueue(const Node& root, std::vector<RenderItem>& out);

// Append to `visible` a pointer to every item whose world bounds intersect
// `frustum`, and return how many survived. Pure — it just reuses
// Frustum::intersectsAabb (the conservative positive-vertex test from Step 19).
//
// IMPORTANT: this is for the CAMERA pass only. The shadow pass must NOT be culled
// to the camera frustum — an object behind the camera can still cast a shadow
// that falls INTO view, so culling its caster would make shadows pop in and out.
[[nodiscard]] inline std::size_t cullToFrustum(const std::vector<RenderItem>& items,
                                               const Frustum& frustum,
                                               std::vector<const RenderItem*>& visible) {
    visible.clear();
    for (const RenderItem& item : items) {
        if (frustum.intersectsAabb(item.worldBounds)) {
            visible.push_back(&item);
        }
    }
    return visible.size();
}

// Split an already-culled `visible` list by the material's AlphaMode (Step 21):
// opaque items into `opaqueOut`, translucent (BLEND) items into `transparentOut`.
// Both outputs are CLEARED first (this replaces, not appends). Opaque items keep the
// queue's original order — the depth buffer resolves their visibility regardless.
//
// When `sortTransparent` is true, `transparentOut` is ordered BACK-TO-FRONT: farthest
// from `cameraPos` first. This is the painter's algorithm, and it's mandatory for
// alpha blending — the "over" operator `src·α + dst·(1-α)` is NOT commutative, so a
// nearer translucent surface must be composited AFTER (on top of) a farther one to
// look right. Passing false leaves them in queue order, which visibly mis-composites
// overlapping translucent objects — useful as an A/B to SEE why the sort exists.
//
// Pure (no GPU): the renderer calls it each frame, and tests exercise it headlessly.
// Distance uses each item's world-bounds CENTRE — a per-object key, so large or
// interpenetrating translucent meshes can still sort wrongly (the classic limitation
// that motivates order-independent transparency).
void partitionByBlend(const std::vector<const RenderItem*>& visible,
                      const Vec3& cameraPos, bool sortTransparent,
                      std::vector<const RenderItem*>& opaqueOut,
                      std::vector<const RenderItem*>& transparentOut);

// ---------------------------------------------------------------------------
//  Draw-call batching (Step 24) — the second thing the render queue unlocks.
// ---------------------------------------------------------------------------
//  Drawing every item as its own call re-binds the material + mesh and pushes a
//  fresh transform each time — pure per-draw overhead. If we SORT the list so
//  identical work is adjacent, we can (a) skip the redundant binds and (b)
//  collapse a run of identical items into ONE instanced draw. These pure helpers
//  produce that plan; the renderer consumes it. No GPU here → unit-tested.

// One instanced draw: `count` copies of `mesh` (with `material`), whose per-copy
// transforms occupy instance-buffer slots [first, first + count). `material` is
// null for the shadow pass, which batches by mesh alone (depth is material-blind).
struct DrawBatch {
    const Mesh*     mesh     = nullptr;
    const Material* material = nullptr;
    std::uint32_t   first    = 0;  // first instance index in the frame's instance buffer
    std::uint32_t   count    = 0;  // number of instances (copies) in this batch
};

// STABLE-sort `items` by (mesh, material) — mesh primary, material secondary — so
// identical draws are adjacent, ready to coalesce. ONE sort serves BOTH passes: the
// color pass coalesces by (mesh, material) (a batch shares the bound textures AND the
// geometry), and the depth-only shadow pass coalesces by mesh ALONE (material is
// irrelevant to depth) — and because mesh is the PRIMARY key, each mesh's items are
// contiguous for the shadow pass while each (mesh, material) group is contiguous for
// the color pass. Stable so equal-key items keep their queue order (deterministic
// frame to frame). Pointer identity is the key: meshes/materials are shared by
// shared_ptr, so same pointer ⇒ same GPU state.
void sortByMeshMaterial(std::vector<const RenderItem*>& items);

// Walk an ALREADY-SORTED list and emit one DrawBatch per maximal run of items that
// share the batch key: (mesh AND material) when `byMaterial` is true, mesh alone
// otherwise. `first` is the run's start index in the list (which the renderer mirrors
// when it packs the instance buffer in the same order), `count` its length. Clears
// `out` first. Pure → tested headlessly in tests/test_render_queue.cpp.
void coalesceBatches(const std::vector<const RenderItem*>& sorted, bool byMaterial,
                     std::vector<DrawBatch>& out);

}  // namespace koi
