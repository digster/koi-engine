// ============================================================================
//  Engine.hpp — the top-level object that ties the subsystems together
// ----------------------------------------------------------------------------
//  The Engine owns the window and the renderer, runs the main loop, and routes
//  input events. Everything else in the engine will eventually hang off this
//  object. The lifecycle is deliberately explicit:
//
//      init()  -> create subsystems (bottom-up)
//      run()   -> loop: handle events, render a frame, repeat
//      shutdown() -> destroy subsystems (top-down, reverse of init)
//
//  We use std::unique_ptr for the subsystems so their lifetimes are tied to the
//  Engine, and so resetting them (in shutdown) triggers their RAII destructors
//  in a controlled, predictable order.
// ============================================================================
#pragma once

#include <memory>
#include <vector>

#include "renderer/PostProcess.hpp"  // PostSettings value member (SDL-free, cheap)
#include "scene/Camera.hpp"  // value member; SDL-free (math only), so cheap to include
#include "scene/Light.hpp"   // std::vector<Light> value member; SDL-free, cheap

namespace koi {

class Window;
class GpuRenderer;
class Node;  // the scene-graph root; full definition in scene/Node.hpp

class Engine {
public:
    // Window/loop settings, grouped so main() reads clearly at the call site.
    struct Config {
        const char* title  = "Koi Engine";
        int         width  = 1280;
        int         height = 720;
    };

    // Constructor and destructor are declared here but DEFINED in Engine.cpp.
    // This matters because our unique_ptr members point to forward-declared
    // types (Window, GpuRenderer): unique_ptr's destructor needs the complete
    // type, which only exists in the .cpp. Defining these out-of-line keeps the
    // forward declarations valid.
    Engine();
    ~Engine();  // safety net: calls shutdown() if the caller forgot to.

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Bring up SDL and all subsystems. Returns false (after logging) if any
    // step fails, so main() can exit cleanly.
    [[nodiscard]] bool init(const Config& config);

    // Run the main loop until the user quits. Blocks until then.
    void run();

    // Tear everything down. Safe to call more than once (and from ~Engine).
    void shutdown();

private:
    // Drain and react to all pending OS/input events for this frame.
    void processEvents();

    // Build the demo scene graph (a ground plane + an animated cube hierarchy).
    // Called from init() after the renderer is valid, since it uploads meshes
    // through the renderer. Returns false (after logging) if a mesh upload fails.
    bool buildScene();

    // Populate lights_ (Step 11): the directional sun at index 0 (the shadow
    // caster), plus a couple of colored point lights and a spotlight. Called from
    // init(); no GPU work, so it can't fail. See scene/Light.hpp.
    void setupLights();

    bool running_     = false;
    bool initialized_ = false;

    std::unique_ptr<Window>      window_;
    std::unique_ptr<GpuRenderer> renderer_;

    // The scene graph. Declared AFTER renderer_ so default destruction (reverse of
    // declaration) frees the scene — and thus every Mesh's GPU buffers — BEFORE
    // the renderer destroys the device those buffers belong to. shutdown() also
    // enforces this order explicitly.
    std::unique_ptr<Node> sceneRoot_;

    // Non-owning handles into sceneRoot_'s tree for the nodes we animate each
    // frame: the hub (its satellites orbit with it) and an inner pivot (its moon
    // orbits it). Owned by the tree, so these are plain observing pointers.
    Node* hub_     = nullptr;
    Node* spinner_ = nullptr;

    // The fly camera we drive from input each frame; its view matrix is handed to
    // the renderer. Held by value (it's small and owns no resources).
    Camera camera_;

    // Post-processing settings (Step 10), toggled from the keyboard in processEvents
    // and handed to the renderer each frame. Defaults enable every effect.
    PostSettings post_;

    // The scene's lights (Step 11). Index 0 is the directional sun (the shadow
    // caster); the rest are colored point/spot lights. The Engine owns them (like
    // the camera and post settings), animates one each frame, toggles them from the
    // keyboard, and hands the list to the renderer to pack into a shader uniform.
    std::vector<Light> lights_;

    // Angle accumulator (radians) for the one orbiting point light, advanced by
    // delta-time each frame so its colored pool sweeps around the scene.
    float lightOrbit_ = 0.0f;
};

}  // namespace koi
