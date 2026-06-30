// ============================================================================
//  Node.hpp — one element of the scene graph (a tree of placed objects)
// ----------------------------------------------------------------------------
//  Step 4 drew a flat, hardcoded list of cube positions. A SCENE GRAPH replaces
//  that list with a TREE: each Node has a local Transform, an optional Mesh to
//  draw, and any number of child Nodes. A child's placement is expressed
//  RELATIVE to its parent, so moving a parent moves all of its descendants with
//  it — the core payoff of the structure.
//
//      world(node) = world(parent) * local(node)
//
//  Example: a "hub" node spins; a "satellite" child sits at +X *in the hub's
//  space*. As the hub rotates, the child's WORLD position sweeps in a circle —
//  we never touch the child's own transform. Nest one more level and a
//  grandchild inherits both. This is how articulated things (a turret on a tank,
//  a moon around a planet, a hand on an arm) are built.
//
//  TWO PASSES PER FRAME (kept separate on purpose):
//    1. updateWorldTransforms() walks the tree top-down and caches each node's
//       world matrix (this file).
//    2. The renderer walks the tree and DRAWS, reading those cached matrices
//       (GpuRenderer::recordScene). Separating "compute transforms" from "draw"
//       mirrors how real engines are organised and keeps each side simple.
//
//  OWNERSHIP
//    * children: std::unique_ptr — a Node solely owns its children, so the whole
//      tree frees itself when the root is destroyed.
//    * mesh / material: std::shared_ptr — both are SHARED. Many nodes can point at
//      one cube Mesh (uploaded once) and one Material (Step 8). A node may have NO
//      mesh: a pure "group"/"pivot" used only to position or spin its children. A
//      node draws only if it has BOTH a mesh (shape) and a material (appearance).
// ============================================================================
#pragma once

#include <memory>
#include <vector>

#include "math/Mat4.hpp"
#include "renderer/Mesh.hpp"     // Node references GPU geometry → scene depends on renderer here
#include "scene/Material.hpp"    // the surface appearance a node draws with
#include "scene/Transform.hpp"

namespace koi {

class Node {
public:
    // A bare node (no geometry) — useful as the scene root or a group/pivot.
    Node() = default;

    // A node that draws `mesh`. Pass nullptr for a group node (same as Node()).
    explicit Node(std::shared_ptr<Mesh> mesh);

    // A node that draws `mesh` with `material` — the usual drawable node.
    Node(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material);

    // Mutable access to the local placement, so callers (and the animation loop)
    // can read/tweak position, rotation, and scale directly.
    [[nodiscard]] Transform&       transform()       { return transform_; }
    [[nodiscard]] const Transform& transform() const { return transform_; }

    // The geometry this node draws, or nullptr for a group node.
    void setMesh(std::shared_ptr<Mesh> mesh) { mesh_ = std::move(mesh); }
    [[nodiscard]] const Mesh* mesh() const { return mesh_.get(); }

    // The appearance this node draws with, or nullptr if not set. A node is only
    // drawn when it has both a mesh and a material.
    void setMaterial(std::shared_ptr<Material> material) { material_ = std::move(material); }
    [[nodiscard]] const Material* material() const { return material_.get(); }

    // Attach a child (this node takes ownership). Returns a non-owning pointer to
    // the child so callers can keep building the tree / animate it later.
    Node* addChild(std::unique_ptr<Node> child);
    [[nodiscard]] const std::vector<std::unique_ptr<Node>>& children() const { return children_; }

    // The cached world matrix from the last updateWorldTransforms() pass.
    [[nodiscard]] const Mat4& worldMatrix() const { return world_; }

    // Recompute this node's world matrix (parentWorld * localMatrix()) and
    // recurse into every child. Call once per frame on the root before rendering.
    // The root's parent is the identity (its local space *is* world space).
    void updateWorldTransforms(const Mat4& parentWorld = Mat4::identity());

private:
    Transform                          transform_;                     // local (relative to parent)
    Mat4                               world_ = Mat4::identity();      // cached absolute placement
    std::shared_ptr<Mesh>              mesh_;                          // shared shape; may be null
    std::shared_ptr<Material>          material_;                      // shared appearance; may be null
    std::vector<std::unique_ptr<Node>> children_;                     // owned subtree
};

}  // namespace koi
