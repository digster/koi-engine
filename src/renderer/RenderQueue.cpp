#include "renderer/RenderQueue.hpp"

#include "renderer/Mesh.hpp"   // Mesh::localBounds()
#include "scene/Node.hpp"      // Node traversal + cached world matrix

namespace koi {

void buildRenderQueue(const Node& root, std::vector<RenderItem>& out) {
    // A node is drawable only if it has BOTH geometry (mesh) and an appearance
    // (material) — the same rule recordNode used to draw by. Pure group/pivot
    // nodes contribute no item; their transform already lives in every
    // descendant's cached world matrix (Node::updateWorldTransforms).
    const Mesh*     mesh     = root.mesh();
    const Material* material = root.material();
    if (mesh != nullptr && material != nullptr) {
        const Mat4& world = root.worldMatrix();
        // Transform the mesh's model-space box into world space ONCE here, so the
        // per-frame culling test is a handful of plane evaluations with no box
        // rebuild. Aabb::transformed re-expands over the 8 transformed corners
        // (Step 19), which stays a valid — if slightly loose — world AABB.
        out.push_back(RenderItem{mesh, material, world, mesh->localBounds().transformed(world)});
    }

    // Flatten the rest of the subtree in the same depth-first order recordNode
    // walked, so — absent sorting — the queue draws in the exact sequence the
    // recursive path did (a property the byte-stable capture relies on).
    for (const std::unique_ptr<Node>& child : root.children()) {
        buildRenderQueue(*child, out);
    }
}

}  // namespace koi
