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

#include "scene/Camera.hpp"  // value member; SDL-free (math only), so cheap to include

namespace koi {

class Window;
class GpuRenderer;

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

    bool running_     = false;
    bool initialized_ = false;

    std::unique_ptr<Window>      window_;
    std::unique_ptr<GpuRenderer> renderer_;

    // The fly camera we drive from input each frame; its view matrix is handed to
    // the renderer. Held by value (it's small and owns no resources).
    Camera camera_;
};

}  // namespace koi
