#include "core/Engine.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <string>

#include "core/Log.hpp"
#include "core/Window.hpp"
#include "renderer/GpuRenderer.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Primitives.hpp"
#include "renderer/Texture.hpp"
#include "scene/Node.hpp"

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

    // The renderer is ready, so it can now manufacture meshes. Build the scene
    // graph that the main loop will animate and the renderer will draw.
    if (!buildScene()) {
        shutdown();
        return false;
    }

    // Load the texture the scene is sampled from (a checkerboard / UV grid),
    // resolved relative to the executable — the same convention the shaders use.
    const std::string texturePath =
        std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "") + "assets/uv_grid.bmp";
    texture_ = renderer_->loadTexture(texturePath.c_str());
    if (!texture_) {
        shutdown();
        return false;
    }

    initialized_ = true;
    running_     = true;
    KOI_INFO("Engine initialized — entering main loop.");
    return true;
}

bool Engine::buildScene() {
    // Two shared meshes, uploaded to the GPU once and reused by every node that
    // wants them. The cube is the RGB color cube from earlier steps; the plane is
    // a flat ground. shared_ptr lets many nodes point at the same geometry.
    std::shared_ptr<Mesh> cube  = makeCubeMesh(*renderer_);
    std::shared_ptr<Mesh> plane = makePlaneMesh(*renderer_);
    if (!cube || !plane) {
        KOI_ERROR("buildScene: failed to create one or more meshes.");
        return false;
    }

    // The root is a pure GROUP node (no mesh): a stable parent for the whole world.
    sceneRoot_ = std::make_unique<Node>();

    // A ground plane, dropped a couple of units below the cubes so they sit above
    // it. (Its own local transform places it; the mesh itself is centered at y=0.)
    auto ground = std::make_unique<Node>(plane);
    ground->transform().position = {0.0f, -2.0f, 0.0f};
    sceneRoot_->addChild(std::move(ground));

    // The HUB: a cube at the origin that spins about Y (animated in run()). Every
    // node below is placed in the hub's space, so they orbit it as it turns — the
    // whole point of a scene graph.
    hub_ = sceneRoot_->addChild(std::make_unique<Node>(cube));

    // Two small satellites at fixed offsets in the hub's space. As the hub spins,
    // they sweep around it in a circle, though their OWN transforms never change.
    auto satA = std::make_unique<Node>(cube);
    satA->transform().position = {2.5f, 0.0f, 0.0f};
    satA->transform().scale    = {0.5f, 0.5f, 0.5f};
    hub_->addChild(std::move(satA));

    auto satC = std::make_unique<Node>(cube);
    satC->transform().position = {0.0f, 0.0f, 2.5f};
    satC->transform().scale    = {0.5f, 0.5f, 0.5f};
    hub_->addChild(std::move(satC));

    // A third satellite that ALSO spins (the "spinner"/pivot), carrying a
    // grandchild "moon" offset from it. Result: the moon orbits the spinner
    // (because the spinner turns) WHILE the spinner orbits the hub (because the
    // hub turns) — two levels of inherited motion, with no special code.
    auto pivot = std::make_unique<Node>(cube);
    pivot->transform().position = {-2.5f, 0.0f, 0.0f};
    pivot->transform().scale    = {0.6f, 0.6f, 0.6f};
    spinner_ = hub_->addChild(std::move(pivot));

    auto moon = std::make_unique<Node>(cube);
    moon->transform().position = {2.0f, 0.0f, 0.0f};
    moon->transform().scale    = {0.6f, 0.6f, 0.6f};
    spinner_->addChild(std::move(moon));

    KOI_INFO("Scene built: ground plane + animated cube hierarchy (hub, 3 satellites, 1 moon).");
    return true;
}

void Engine::run() {
    // Headless capture mode: if KOI_CAPTURE=<path> is set, render a single frame
    // into an image file and exit immediately, without entering the live loop.
    // This is our visual-debugging tool — it saves exactly what the engine draws
    // without needing screen-recording access. (See CLAUDE.md / docs.)
    if (const char* capturePath = SDL_getenv("KOI_CAPTURE")) {
        // Resolve the scene's world matrices once (the animation hasn't run, so
        // this captures the deterministic t = 0 pose) before drawing it off-screen.
        sceneRoot_->updateWorldTransforms();
        if (renderer_->captureFrame(capturePath, kClearColor, camera_.viewMatrix(), *sceneRoot_, *texture_)) {
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

        // Animate the scene: spin the hub (its satellites orbit with it) and the
        // inner pivot (its moon orbits it). Angles accumulate in radians, scaled by
        // dt so the motion is frame-rate independent — just like the camera.
        hub_->transform().rotationEuler.y     += 0.6f * dt;
        spinner_->transform().rotationEuler.y += 1.5f * dt;

        // Propagate transforms down the tree (parent → child → grandchild) so every
        // node's cached world matrix is current, THEN draw.
        sceneRoot_->updateWorldTransforms();
        renderer_->renderFrame(kClearColor, camera_.viewMatrix(), *sceneRoot_, *texture_);

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

    // Destroy in reverse order of creation. The ORDER HERE MATTERS: the scene and
    // the texture each hold GPU resources freed through the renderer's device — so
    // they must die BEFORE the renderer destroys that device. (Resetting a smart
    // pointer runs the held object's destructor immediately.)
    sceneRoot_.reset();      // frees all nodes + meshes (the meshes' GPU buffers)
    hub_     = nullptr;      // dangling now that the tree is gone
    spinner_ = nullptr;
    texture_.reset();        // frees the texture's GPU image
    renderer_.reset();       // detaches the swapchain, then destroys the device
    window_.reset();

    // Log before SDL_Quit(): SDL_Quit resets logging state, which can suppress
    // messages emitted after it.
    KOI_INFO("Engine shut down cleanly.");
    SDL_Quit();

    running_     = false;
    initialized_ = false;
}

}  // namespace koi
