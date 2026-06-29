// ============================================================================
//  test_node.cpp — unit tests for the scene graph (Node world transforms)
// ----------------------------------------------------------------------------
//  The scene graph's defining behaviour is that a parent's transform propagates
//  to its descendants: world(node) = world(parent) * local(node). We test that
//  composition directly, with NO GPU involved — every node here has a null mesh
//  (a pure group/pivot), so these tests need only the math.
//
//  Trick: a node's world-space POSITION is its world matrix applied to the local
//  origin (0,0,0,1) — i.e. the matrix's translation column. We compare that to
//  the position we expect after stacking the hierarchy.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include <memory>

#include "math/Mat4.hpp"
#include "math/Vec.hpp"
#include "scene/Node.hpp"

using namespace koi;

// The world position of a node = its world matrix times the local origin.
static Vec4 worldOrigin(const Node& n) {
    return n.worldMatrix() * Vec4{0, 0, 0, 1};
}

TEST_CASE("a lone node's world matrix is just its local transform") {
    Node root;
    root.transform().position = {1, 2, 3};
    root.updateWorldTransforms();

    const Vec4 p = worldOrigin(root);
    CHECK(p.x == doctest::Approx(1.0f));
    CHECK(p.y == doctest::Approx(2.0f));
    CHECK(p.z == doctest::Approx(3.0f));
}

TEST_CASE("a child's world position stacks onto its parent's") {
    auto root = std::make_unique<Node>();
    root->transform().position = {10, 0, 0};
    Node* child = root->addChild(std::make_unique<Node>());
    child->transform().position = {0, 5, 0};  // expressed in the parent's space

    root->updateWorldTransforms();

    const Vec4 p = worldOrigin(*child);
    CHECK(p.x == doctest::Approx(10.0f));
    CHECK(p.y == doctest::Approx(5.0f));
    CHECK(p.z == doctest::Approx(0.0f));
}

TEST_CASE("a grandchild inherits the whole chain") {
    auto root = std::make_unique<Node>();
    root->transform().position = {1, 0, 0};
    Node* child = root->addChild(std::make_unique<Node>());
    child->transform().position = {0, 1, 0};
    Node* grand = child->addChild(std::make_unique<Node>());
    grand->transform().position = {0, 0, 1};

    root->updateWorldTransforms();

    const Vec4 p = worldOrigin(*grand);
    CHECK(p.x == doctest::Approx(1.0f));
    CHECK(p.y == doctest::Approx(1.0f));
    CHECK(p.z == doctest::Approx(1.0f));
}

TEST_CASE("rotating a parent sweeps its child's world position") {
    // Parent at the origin rotated 90° about Y; the child sits at +X (2,0,0) in
    // the parent's space. rotationY(90°) maps +X to -Z (right-handed), so the
    // child's WORLD position should be about (0, 0, -2) — it orbited the parent
    // without its own transform ever changing.
    auto root = std::make_unique<Node>();
    root->transform().rotationEuler = {0, radians(90.0f), 0};
    Node* child = root->addChild(std::make_unique<Node>());
    child->transform().position = {2, 0, 0};

    root->updateWorldTransforms();

    const Vec4 p = worldOrigin(*child);
    CHECK(p.x == doctest::Approx(0.0f));
    CHECK(p.y == doctest::Approx(0.0f));
    CHECK(p.z == doctest::Approx(-2.0f));
}

TEST_CASE("a parent's scale also scales its children's offsets") {
    // A frequently-surprising consequence of world = parent * local: scaling a
    // parent stretches the SPACE its children live in, so their offsets grow too.
    auto root = std::make_unique<Node>();
    root->transform().scale = {2, 2, 2};
    Node* child = root->addChild(std::make_unique<Node>());
    child->transform().position = {1, 0, 0};

    root->updateWorldTransforms();

    const Vec4 p = worldOrigin(*child);
    CHECK(p.x == doctest::Approx(2.0f));  // 1 unit offset, doubled by the parent
    CHECK(p.y == doctest::Approx(0.0f));
    CHECK(p.z == doctest::Approx(0.0f));
}
