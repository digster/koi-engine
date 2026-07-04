// ============================================================================
//  Engine.hpp — the reusable core: window, renderer, and the main loop
// ----------------------------------------------------------------------------
//  The Engine owns the machinery every app needs but none should re-implement:
//  SDL, the window, the GPU renderer, the delta-time clock, and the frame loop
//  (including the headless KOI_CAPTURE / KOI_MAX_FRAMES paths). It owns NO game
//  content — no scene, no lights, no camera. Those belong to an Application
//  (see core/Application.hpp), which the Engine DRIVES by calling back into its
//  hooks. That inversion is what makes koi_core a library rather than a program:
//  the demo under samples/ is just one Application among many possible ones.
//
//  Lifecycle is deliberately explicit:
//      init(config)  -> create subsystems (bottom-up: SDL -> window -> renderer)
//      run(app)      -> onStart, then loop: events -> update -> render
//      shutdown()    -> destroy subsystems (top-down, reverse of init)
//
//  unique_ptr holds the subsystems so their lifetimes track the Engine and their
//  RAII destructors run in a controlled order when reset (in shutdown).
// ============================================================================
#pragma once

#include <memory>

namespace koi {

class Window;
class GpuRenderer;
class Application;  // the app the engine drives; full definition in core/Application.hpp

class Engine {
public:
    // Window/loop settings, grouped so the app's main() reads clearly at the call site.
    struct Config {
        const char* title  = "Koi Engine";
        int         width  = 1280;
        int         height = 720;
        // Capture the mouse for FPS-style look on interactive runs (hidden, locked,
        // relative deltas). Automatically skipped under KOI_MAX_FRAMES so headless
        // smoke tests never grab the user's cursor. Set false for a normal cursor.
        bool        captureMouse = true;
    };

    // Constructor and destructor are declared here but DEFINED in Engine.cpp, so the
    // unique_ptr members (pointing at forward-declared Window/GpuRenderer) see the
    // complete types where they're destroyed.
    Engine();
    ~Engine();  // safety net: calls shutdown() if the caller forgot to.

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Bring up SDL and all subsystems. Returns false (after logging) if any step
    // fails, so the app's main() can exit cleanly.
    [[nodiscard]] bool init(const Config& config);

    // Run `app` to completion: call app.onStart once, then loop (events -> update ->
    // render) until a quit is requested — or, under KOI_CAPTURE, render a single
    // frame to a file and return. app.onShutdown() is called before returning, while
    // the renderer is still alive (so the app can free its GPU resources in time).
    // Returns false if onStart failed or the capture failed.
    bool run(Application& app);

    // Tear everything down. Safe to call more than once (and from ~Engine).
    void shutdown();

    // --- Services the running Application reaches through the Engine ----------

    // The GPU renderer — the app uses it in onStart to build meshes/textures and at
    // runtime to toggle renderer state (skybox, IBL). Defined out-of-line so this
    // header needn't pull in the full GpuRenderer definition.
    [[nodiscard]] GpuRenderer& renderer();

    // Ask the loop to stop after the current frame. The app calls this to quit
    // (e.g. on Escape); the engine also stops on an OS quit (window close).
    void requestQuit() { running_ = false; }

private:
    // Drain this frame's OS/input events: handle the OS quit itself, and forward
    // EVERY event to the app so it can react (toggles, mouse-look, Escape, ...).
    void processEvents(Application& app);

    bool running_     = false;
    bool initialized_ = false;
    Config config_{};  // kept from init() so run() can honour e.g. captureMouse

    std::unique_ptr<Window>      window_;
    std::unique_ptr<GpuRenderer> renderer_;
};

}  // namespace koi
