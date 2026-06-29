#include "core/Engine.hpp"

#include <SDL3/SDL.h>

#include "core/Log.hpp"
#include "core/Window.hpp"
#include "renderer/GpuRenderer.hpp"

namespace koi {

// The colour the screen is cleared to every frame. SDL_FColor components are
// floats in [0, 1]. This is a calm dark teal — distinctive enough that you can
// immediately tell the GPU clear actually ran.
namespace {
constexpr SDL_FColor kClearColor = {0.04f, 0.10f, 0.12f, 1.0f};
}

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
    KOI_INFO("Engine initialized — entering main loop.");
    return true;
}

void Engine::run() {
    // Headless capture mode: if KOI_CAPTURE=<path> is set, render a single frame
    // into an image file and exit immediately, without entering the live loop.
    // This is our visual-debugging tool — it saves exactly what the engine draws
    // without needing screen-recording access. (See CLAUDE.md / docs.)
    if (const char* capturePath = SDL_getenv("KOI_CAPTURE")) {
        if (renderer_->captureFrame(capturePath, kClearColor, camera_.viewMatrix())) {
            KOI_INFO("Captured frame to '%s'.", capturePath);
        } else {
            KOI_ERROR("Frame capture failed.");
        }
        running_ = false;
        return;
    }

    // Optional frame cap for automated/headless smoke tests. If the environment
    // variable KOI_MAX_FRAMES is set to a positive number, we render exactly
    // that many frames and then quit through the normal shutdown path. When it
    // is unset (the usual case) the loop runs until the user quits.
    Uint64 maxFrames = 0;
    if (const char* env = SDL_getenv("KOI_MAX_FRAMES")) {
        maxFrames = SDL_strtoull(env, nullptr, 10);
    }
    Uint64 frame = 0;

    // For an interactive run, capture the mouse: SDL hides the cursor, locks it to
    // the window, and reports motion as relative deltas — exactly what FPS-style
    // mouse look needs. We skip this for headless smoke tests (KOI_MAX_FRAMES) so
    // automated runs don't grab the user's cursor.
    if (maxFrames == 0) {
        SDL_SetWindowRelativeMouseMode(window_->handle(), true);
        KOI_INFO("Controls: WASD move, E/Q up/down, mouse look, Esc to quit.");
    }

    // Delta-time clock: the time since the previous frame, in seconds. Movement is
    // scaled by this so the camera travels the same distance per second whether the
    // machine renders at 60 or 600 FPS. SDL_GetTicksNS gives nanosecond precision.
    Uint64 lastTimeNs = SDL_GetTicksNS();

    // The classic game loop: process input, update, render, repeat.
    while (running_) {
        processEvents();

        const Uint64 nowNs = SDL_GetTicksNS();
        float dt = static_cast<float>(nowNs - lastTimeNs) / 1.0e9f;
        lastTimeNs = nowNs;
        // Clamp: after a breakpoint or a long stall, a huge dt would teleport the
        // camera. Capping it keeps motion sane.
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        // Continuous input: read the CURRENT keyboard state (not key-down events)
        // so held keys produce smooth, repeat-rate-independent movement.
        camera_.processKeyboard(SDL_GetKeyboardState(nullptr), dt);

        renderer_->renderFrame(kClearColor, camera_.viewMatrix());

        if (maxFrames != 0 && ++frame >= maxFrames) {
            KOI_INFO("Reached KOI_MAX_FRAMES=%llu — exiting.",
                     static_cast<unsigned long long>(maxFrames));
            running_ = false;
        }
    }
}

void Engine::processEvents() {
    // SDL_PollEvent pulls one queued event at a time and returns false when the
    // queue is empty, so this loop drains all events accumulated this frame.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                // The window's close button (or OS quit request).
                KOI_INFO("Quit requested — shutting down.");
                running_ = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                // In SDL3, event.key.key holds the virtual keycode.
                if (event.key.key == SDLK_ESCAPE) {
                    KOI_INFO("Escape pressed — shutting down.");
                    running_ = false;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                // Relative mouse mode reports motion as deltas (xrel/yrel). We feed
                // them straight into the camera as a yaw/pitch change — FPS look.
                camera_.addMouseLook(event.motion.xrel, event.motion.yrel);
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                // The swapchain (and our depth texture) auto-resize, and the
                // renderer recomputes the aspect ratio from the swapchain size each
                // frame, so there's nothing to do here but log it.
                KOI_DEBUG("Window resized to %dx%d", event.window.data1, event.window.data2);
                break;

            default:
                break;
        }
    }
}

void Engine::shutdown() {
    if (!initialized_ && !window_ && !renderer_) {
        return;  // already torn down (or never initialized)
    }

    // Destroy in reverse order of creation: the renderer (which detaches its
    // swapchain from the window) before the window itself. Resetting a
    // unique_ptr runs the held object's destructor immediately.
    renderer_.reset();
    window_.reset();

    // Log before SDL_Quit(): SDL_Quit resets logging state, which can suppress
    // messages emitted after it.
    KOI_INFO("Engine shut down cleanly.");
    SDL_Quit();

    running_     = false;
    initialized_ = false;
}

}  // namespace koi
