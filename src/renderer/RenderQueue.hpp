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

}  // namespace koi
