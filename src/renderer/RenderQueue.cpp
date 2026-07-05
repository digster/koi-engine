#include "renderer/RenderQueue.hpp"

#include <algorithm>  // std::stable_sort — the back-to-front transparency sort

#include "renderer/Mesh.hpp"   // Mesh::localBounds()
#include "scene/Material.hpp"  // Material::alphaMode — the opaque/blend split
#include "scene/Node.hpp"      // Node traversal + cached world matrix

namespace koi {

namespace {
// Squared distance from the camera to an item's world-bounds centre. Squared (no
// sqrt) is enough to ORDER items and avoids the wasted root — monotonic in the true
// distance, so the sort result is identical. Local: only partitionByBlend needs it.
[[nodiscard]] float distanceSqToCamera(const RenderItem& item, const Vec3& cameraPos) {
    const Vec3 d = item.worldBounds.center() - cameraPos;
    return dot(d, d);
}
}  // namespace

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

void partitionByBlend(const std::vector<const RenderItem*>& visible,
                      const Vec3& cameraPos, bool sortTransparent,
                      std::vector<const RenderItem*>& opaqueOut,
                      std::vector<const RenderItem*>& transparentOut) {
    opaqueOut.clear();
    transparentOut.clear();

    // One pass over the culled list, bucketing by the material's alpha mode. Both
    // buckets stay in the queue's original (depth-first) order for now.
    for (const RenderItem* item : visible) {
        if (item->material->alphaMode == AlphaMode::Blend) {
            transparentOut.push_back(item);
        } else {
            opaqueOut.push_back(item);
        }
    }

    if (!sortTransparent) {
        return;  // leave them in queue order (the A/B "wrong" case)
    }

    // Back-to-front: sort by DESCENDING distance from the camera so the farthest
    // translucent surface is drawn first and nearer ones composite on top. A STABLE
    // sort keeps equidistant items in their queue order, so the capture stays
    // deterministic frame to frame.
    std::stable_sort(transparentOut.begin(), transparentOut.end(),
                     [&cameraPos](const RenderItem* a, const RenderItem* b) {
                         return distanceSqToCamera(*a, cameraPos) >
                                distanceSqToCamera(*b, cameraPos);
                     });
}

}  // namespace koi
