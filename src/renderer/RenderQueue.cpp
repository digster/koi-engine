#include "renderer/RenderQueue.hpp"

#include <algorithm>   // std::stable_sort — the back-to-front transparency sort
#include <functional>  // std::less<const void*> — a defined total order over pointers

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

void sortByMeshMaterial(std::vector<const RenderItem*>& items) {
    // std::less<const void*> gives a well-defined TOTAL ORDER over unrelated pointers
    // (raw `<` on pointers into different objects is not portably ordered). We only
    // need *an* order that puts equal keys together — the values are meaningless.
    // Mesh is the PRIMARY key so the shadow pass (which groups by mesh alone) sees
    // contiguous mesh runs; material is the secondary key so the color pass's
    // (mesh, material) groups are contiguous too.
    std::less<const void*> before;
    std::stable_sort(items.begin(), items.end(),
                     [&before](const RenderItem* a, const RenderItem* b) {
                         if (a->mesh != b->mesh) {
                             return before(a->mesh, b->mesh);
                         }
                         return before(a->material, b->material);
                     });
}

void coalesceBatches(const std::vector<const RenderItem*>& sorted, bool byMaterial,
                     std::vector<DrawBatch>& out) {
    out.clear();
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const RenderItem* item = sorted[i];
        // Extend the current run if this item shares its key with the previous one;
        // otherwise start a new batch. (Only valid because the list is pre-sorted, so
        // all items with a given key are contiguous.)
        const bool extends = !out.empty() && out.back().mesh == item->mesh &&
                             (!byMaterial || out.back().material == item->material);
        if (extends) {
            out.back().count += 1;
        } else {
            out.push_back(DrawBatch{item->mesh,
                                    byMaterial ? item->material : nullptr,
                                    static_cast<std::uint32_t>(i), 1});
        }
    }
}

}  // namespace koi
