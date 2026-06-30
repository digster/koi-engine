#include "scene/Node.hpp"

namespace koi {

Node::Node(std::shared_ptr<Mesh> mesh) : mesh_(std::move(mesh)) {}

Node::Node(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material)
    : mesh_(std::move(mesh)), material_(std::move(material)) {}

Node* Node::addChild(std::unique_ptr<Node> child) {
    Node* raw = child.get();
    children_.push_back(std::move(child));
    return raw;
}

void Node::updateWorldTransforms(const Mat4& parentWorld) {
    // Compose our local placement onto the parent's world matrix. Because we pass
    // OUR freshly-computed world matrix down as each child's parentWorld, a single
    // top-down pass resolves the whole hierarchy: a transform applied to the root
    // ripples out to every descendant.
    world_ = parentWorld * transform_.localMatrix();
    for (const std::unique_ptr<Node>& child : children_) {
        child->updateWorldTransforms(world_);
    }
}

}  // namespace koi
