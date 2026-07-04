#include "core/Engine.hpp"

#include <SDL3/SDL.h>

#include "core/Application.hpp"
#include "core/Log.hpp"
#include "core/Window.hpp"
#include "renderer/FrameView.hpp"
#include "renderer/GpuRenderer.hpp"

namespace koi {

// Defined here (not in the header) so the unique_ptr members see the complete
// definitions of Window and GpuRenderer, included above.
Engine::Engine() = default;

Engine::~Engine() {
    // RAII safety net: if run() returned and the caller forgot shutdown(), we
    // still release everything here.
    shutdown();
}

bool Engine::init(const Config& config) {
    // Configure logging first so every subsequent step can report itself.
    // Verbose (DEBUG-level) logging is on; in a Release build we'd pass false.
    koi::log::init(/*verbose=*/true);
    config_ = config;  // remember settings run() needs later (e.g. captureMouse)

    // SDL_Init starts the subsystems we ask for. SDL_INIT_VIDEO covers windows,
    // the event queue, and the display/GPU plumbing we need.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        KOI_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    KOI_INFO("SDL initialized (version %d)", SDL_GetVersion());

    // Subsystems are created bottom-up: the window first, then the renderer that
    // attaches to it.
    window_ = std::make_unique<Window>(config.title, config.width, config.height);
    if (!window_->isValid()) {
        shutdown();
        return false;
    }

    renderer_ = std::make_unique<GpuRenderer>(window_->handle());
    if (!renderer_->isValid()) {
        shutdown();
        return false;
    }

    initialized_ = true;
    running_     = true;
    KOI_INFO("Engine initialized — ready to run an application.");
    return true;
}

GpuRenderer& Engine::renderer() {
    // Precondition: init() succeeded, so renderer_ is non-null. The app only ever
    // reaches this from onStart/onEvent, both driven from run() after init().
    return *renderer_;
}

bool Engine::run(Application& app) {
    // Hand control to the app to build its world (scene, camera, lights) now that
    // the renderer exists. If it can't start, tear it down and bail.
    if (!app.onStart(*this)) {
        KOI_ERROR("Application onStart failed — aborting run.");
        app.onShutdown();  // let it release anything it half-built, device still alive
        running_ = false;
        return false;
    }

    // Headless capture mode: if KOI_CAPTURE=<path> is set, render a single frame
    // into an image file and return, without entering the live loop. This is our
    // visual-debugging tool — it saves exactly what the app draws without needing
    // screen-recording access. The app is expected to have its scene posed (world
    // transforms up to date) by the end of onStart. (See CLAUDE.md / docs.)
    if (const char* capturePath = SDL_getenv("KOI_CAPTURE")) {
        const FrameView fv = app.frameView();
        bool ok = fv.root != nullptr && renderer_->captureFrame(capturePath, fv);
        if (ok) {
            KOI_INFO("Captured frame to '%s'.", capturePath);
        } else {
            KOI_ERROR("Frame capture failed.");
        }
        app.onShutdown();  // free the app's GPU resources while the renderer lives
        running_ = false;
        return ok;
    }

    // Optional frame cap for automated/headless smoke tests. If KOI_MAX_FRAMES is a
    // positive number, we render exactly that many frames and then quit through the
    // normal path. Unset (the usual case) means "run until the user quits".
    Uint64 maxFrames = 0;
    if (const char* env = SDL_getenv("KOI_MAX_FRAMES")) {
        maxFrames = SDL_strtoull(env, nullptr, 10);
    }
    Uint64 frame = 0;

    // For an interactive run, capture the mouse: SDL hides the cursor, locks it to
    // the window, and reports motion as relative deltas — exactly what FPS-style
    // mouse look needs. We skip it for headless smoke tests (KOI_MAX_FRAMES) so
    // automated runs don't grab the user's cursor, and honour Config::captureMouse.
    if (maxFrames == 0 && config_.captureMouse) {
        SDL_SetWindowRelativeMouseMode(window_->handle(), true);
    }

    // Delta-time clock: the time since the previous frame, in seconds. The app
    // scales motion by this so movement is frame-rate independent. SDL_GetTicksNS
    // gives nanosecond precision.
    Uint64 lastTimeNs = SDL_GetTicksNS();

    // The classic game loop: process input, update, render, repeat.
    while (running_) {
        processEvents(app);

        const Uint64 nowNs = SDL_GetTicksNS();
        float dt = static_cast<float>(nowNs - lastTimeNs) / 1.0e9f;
        lastTimeNs = nowNs;
        // Clamp: after a breakpoint or a long stall, a huge dt would teleport
        // anything integrated by it. Capping it keeps motion sane.
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        // Let the app advance its world (animation + continuous input). By the time
        // it returns, its scene's world transforms should be current.
        app.onUpdate(*this, dt);

        // Ask the app what to draw and hand it straight to the renderer.
        const FrameView fv = app.frameView();
        if (fv.root != nullptr) {
            renderer_->renderFrame(fv);
        }

        if (maxFrames != 0 && ++frame >= maxFrames) {
            KOI_INFO("Reached KOI_MAX_FRAMES=%llu — exiting.",
                     static_cast<unsigned long long>(maxFrames));
            running_ = false;
        }
    }

    app.onShutdown();  // free the app's GPU resources while the renderer still lives
    return true;
}

void Engine::processEvents(Application& app) {
    // SDL_PollEvent pulls one queued event at a time and returns false when the
    // queue is empty, so this loop drains all events accumulated this frame.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // The one event the ENGINE owns: an OS quit (window close button / Cmd-Q)
        // stops the loop regardless of what the app does with it.
        if (event.type == SDL_EVENT_QUIT) {
            KOI_INFO("Quit requested — shutting down.");
            running_ = false;
        }
        // Every event is forwarded to the app: it decides what its keys, mouse, and
        // window events mean (toggles, mouse-look, Escape-to-quit, ...).
        app.onEvent(*this, event);
    }
}

void Engine::shutdown() {
    if (!initialized_ && !window_ && !renderer_) {
        return;  // already torn down (or never initialized)
    }

    // Destroy in reverse order of creation. The Application (which owns the scene
    // and thus GPU meshes/textures) is expected to have released those in its
    // onShutdown(), called from run() while the renderer was still alive — so by
    // the time we destroy the device here, nothing borrows it anymore.
    renderer_.reset();  // detaches the swapchain, then destroys the device
    window_.reset();

    // Log before SDL_Quit(): SDL_Quit resets logging state, which can suppress
    // messages emitted after it.
    KOI_INFO("Engine shut down cleanly.");
    SDL_Quit();

    running_     = false;
    initialized_ = false;
}

}  // namespace koi
