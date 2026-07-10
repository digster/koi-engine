// ============================================================================
//  DemoApp.hpp — the Koi showcase, as an Application
// ----------------------------------------------------------------------------
//  This is the demo scene that used to live *inside* the engine. It is now an
//  ordinary koi::Application: it owns the scene graph, the fly camera, the
//  lights, and the post-processing settings, and it implements the hooks the
//  engine calls (onStart/onUpdate/onEvent/frameView/onShutdown). It reaches the
//  GPU only through engine.renderer() — exactly the surface any client app uses.
//
//  It lives under samples/ (not src/) to make the boundary concrete: koi_core
//  knows nothing about this file, and a real game would sit here just the same.
// ============================================================================
#pragma once

#include <memory>
#include <vector>

#include "core/Application.hpp"
#include "math/Mat4.hpp"             // Mat4 — the frozen frustum's view-projection
#include "renderer/DebugDraw.hpp"    // DebugDraw value member (Step 22)
#include "renderer/PostProcess.hpp"  // PostSettings value member
#include "scene/Camera.hpp"          // Camera value member
#include "scene/Light.hpp"           // std::vector<Light> value member

namespace koi {

class Engine;
class GpuRenderer;
class Node;  // scene-graph root, held by unique_ptr; full definition in scene/Node.hpp

// The step 0–16 showcase: a ground plane, an animated cube hierarchy, two loaded
// models, and the Damaged Helmet — lit by a sun + coloured point/spot lights,
// under a skybox with image-based lighting and the full post chain.
class DemoApp final : public Application {
public:
    DemoApp();
    ~DemoApp() override;

    bool onStart(Engine& engine) override;
    void onUpdate(Engine& engine, float dt) override;
    void onEvent(Engine& engine, const SDL_Event& event) override;
    [[nodiscard]] FrameView frameView() const override;
    void onShutdown() override;

private:
    // Build the demo scene graph (ground + cube hierarchy + loaded models). Uploads
    // meshes/textures through the renderer, so it can fail — returns false on error.
    bool buildScene(GpuRenderer& renderer);

    // Place the lights that shade the scene (the sun + coloured point/spot lights).
    void setupLights();

    // Rebuild the immediate-mode debug overlay for this frame (Step 22) from the
    // toggles below: a green AABB around every drawable node, an amber wireframe of
    // the frozen camera frustum, and a coloured cross at each positioned light.
    // Called each frame from onUpdate; the result is handed to the renderer via
    // frameView().debugLines.
    void buildDebugLines();

    // The scene graph. The app owns it; it holds GPU meshes + material textures, so
    // onShutdown() releases it while the engine's renderer (and its device) is alive.
    std::unique_ptr<Node> sceneRoot_;

    // Non-owning handles into sceneRoot_ for the nodes we animate: the hub (its
    // satellites orbit with it) and an inner pivot (its moon orbits it).
    Node* hub_     = nullptr;
    Node* spinner_ = nullptr;

    // The fly camera we drive from input; its view matrix is handed to the renderer.
    Camera camera_;

    // Post-processing settings (Step 10), toggled from the keyboard in onEvent.
    PostSettings post_;

    // The scene's lights (Step 11). Index 0 is the directional sun (the shadow
    // caster); the rest are coloured point/spot lights.
    std::vector<Light> lights_;

    // Debug draw (Step 22). `debug_` collects this frame's overlay lines (rebuilt
    // each frame in buildDebugLines). The three bools are per-category runtime
    // toggles (keys G / F / L). `frozenViewProj_` is the camera view-projection
    // captured when the frustum was frozen (key F), so the wireframe stays put as
    // you fly away — showing the exact volume Step 20's culler tested against.
    DebugDraw debug_;
    bool      debugBounds_       = false;  // G: AABB around every drawable node
    bool      debugFrustum_      = false;  // F: draw the frozen camera frustum
    bool      debugLights_       = false;  // L: a cross at each positioned light
    Mat4      frozenViewProj_    = Mat4::identity();
    bool      haveFrozenFrustum_ = false;  // true once a frustum has been captured

    // Angle accumulators (radians) for the animated Y-spins: the hub, the inner
    // pivot ("spinner"), and the one orbiting point light. Transform stores a
    // quaternion now, so we track each spin angle here and rebuild it per frame.
    float hubSpin_     = 0.0f;
    float spinnerSpin_ = 0.0f;
    float lightOrbit_  = 0.0f;
};

}  // namespace koi
