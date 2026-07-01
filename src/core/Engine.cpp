#include "core/Engine.hpp"

#include <SDL3/SDL.h>

#include <cmath>   // std::cos, std::sin — cone cosines + the orbiting light
#include <memory>
#include <string>

#include "core/Log.hpp"
#include "core/Window.hpp"
#include "renderer/GpuRenderer.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/ModelLoader.hpp"
#include "renderer/Primitives.hpp"
#include "scene/Material.hpp"
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

    // The renderer is ready, so it can now manufacture meshes and textures. Build
    // the scene graph (geometry + materials) that the loop animates and draws.
    if (!buildScene()) {
        shutdown();
        return false;
    }

    // Place the lights that shade the scene (the sun + colored point/spot lights).
    // No GPU work, so this can't fail; done after the scene so positions can relate
    // to where objects sit.
    setupLights();

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

    // Two textures, resolved relative to the executable (same convention as the
    // shaders). They become the albedo images of the materials below.
    const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    auto checkerTex = renderer_->loadTexture((base + "assets/uv_grid.bmp").c_str());
    auto dotsTex    = renderer_->loadTexture((base + "assets/dots.bmp").c_str());
    if (!checkerTex || !dotsTex) {
        KOI_ERROR("buildScene: failed to load one or more textures.");
        return false;
    }

    // Three PBR materials (texture + metallic + roughness). They show that appearance
    // varies PER OBJECT across the two axes of the metallic-roughness model:
    //   * floorMat — a rough DIELECTRIC (non-metal): matte checker, soft dim highlight.
    //   * cubeMat  — a mid-roughness dielectric: the warm dots, a broader gloss.
    //   * hubMat   — a polished METAL (same dots texture): tints its reflection with
    //                the albedo and has no diffuse, so the metal/dielectric split is
    //                obvious against cubeMat's identical texture.
    auto floorMat = std::make_shared<Material>(Material{checkerTex, 0.0f, 0.85f});
    auto cubeMat  = std::make_shared<Material>(Material{dotsTex,    0.0f, 0.55f});
    auto hubMat   = std::make_shared<Material>(Material{dotsTex,    1.0f, 0.30f});

    // Step 9: load two MODELS from files (geometry only — we give them our own
    // materials). sphere.glb exercises the cgltf loader; torus.obj the tinyobjloader
    // one. Both produce a Mesh exactly like a primitive does.
    std::shared_ptr<Mesh> sphere = loadModel(*renderer_, (base + "assets/sphere.glb").c_str());
    std::shared_ptr<Mesh> torus  = loadModel(*renderer_, (base + "assets/torus.obj").c_str());
    if (!sphere || !torus) {
        KOI_ERROR("buildScene: failed to load one or more models.");
        return false;
    }
    // The loaded models flank the cubes and headline the two material extremes:
    //   * sphereMat — a smooth METAL (the checker as its reflected tint): crisp,
    //                 bright light reflections that sharpen as you orbit it.
    //   * torusMat  — a glossy DIELECTRIC: a tight white specular over a matte body.
    auto sphereMat = std::make_shared<Material>(Material{checkerTex, 1.0f, 0.20f});
    auto torusMat  = std::make_shared<Material>(Material{dotsTex,    0.0f, 0.25f});

    // The root is a pure GROUP node (no mesh/material): a stable parent for the world.
    sceneRoot_ = std::make_unique<Node>();

    // A ground plane (checker, matte), dropped a couple of units below the cubes so
    // they sit above it. (Its own local transform places it; the mesh is centered.)
    auto ground = std::make_unique<Node>(plane, floorMat);
    ground->transform().position = {0.0f, -2.0f, 0.0f};
    sceneRoot_->addChild(std::move(ground));

    // The HUB: a cube at the origin that spins about Y (animated in run()), with the
    // extra-shiny material. Every node below is placed in the hub's space, so they
    // orbit it as it turns — the whole point of a scene graph.
    hub_ = sceneRoot_->addChild(std::make_unique<Node>(cube, hubMat));

    // Two small satellites (glossy dots) at fixed offsets in the hub's space. As the
    // hub spins, they sweep around it, though their OWN transforms never change.
    auto satA = std::make_unique<Node>(cube, cubeMat);
    satA->transform().position = {2.5f, 0.0f, 0.0f};
    satA->transform().scale    = {0.5f, 0.5f, 0.5f};
    hub_->addChild(std::move(satA));

    auto satC = std::make_unique<Node>(cube, cubeMat);
    satC->transform().position = {0.0f, 0.0f, 2.5f};
    satC->transform().scale    = {0.5f, 0.5f, 0.5f};
    hub_->addChild(std::move(satC));

    // A third satellite that ALSO spins (the "spinner"/pivot), carrying a
    // grandchild "moon" offset from it. Result: the moon orbits the spinner
    // (because the spinner turns) WHILE the spinner orbits the hub (because the
    // hub turns) — two levels of inherited motion, with no special code.
    auto pivot = std::make_unique<Node>(cube, cubeMat);
    pivot->transform().position = {-2.5f, 0.0f, 0.0f};
    pivot->transform().scale    = {0.6f, 0.6f, 0.6f};
    spinner_ = hub_->addChild(std::move(pivot));

    auto moon = std::make_unique<Node>(cube, cubeMat);
    moon->transform().position = {2.0f, 0.0f, 0.0f};
    moon->transform().scale    = {0.6f, 0.6f, 0.6f};
    spinner_->addChild(std::move(moon));

    // The two loaded models, flanking the cube hierarchy and resting on the floor.
    auto sphereNode = std::make_unique<Node>(sphere, sphereMat);
    sphereNode->transform().position = {-3.8f, -0.8f, 0.0f};
    sphereNode->transform().scale    = {1.2f, 1.2f, 1.2f};
    sceneRoot_->addChild(std::move(sphereNode));

    auto torusNode = std::make_unique<Node>(torus, torusMat);
    torusNode->transform().position      = {3.8f, -0.3f, 0.0f};
    torusNode->transform().scale         = {1.2f, 1.2f, 1.2f};
    torusNode->transform().rotationEuler = {radians(90.0f), 0.0f, 0.0f};  // stand the donut up
    sceneRoot_->addChild(std::move(torusNode));

    KOI_INFO("Scene built: ground + cube hierarchy + 2 loaded models (sphere.glb, torus.obj).");
    return true;
}

void Engine::setupLights() {
    lights_.clear();

    // Light 0 — the SUN: a directional light (parallel rays, no distance falloff).
    // This is the ONLY shadow caster — by convention it lives at index 0, which the
    // renderer/shader rely on. Step 12 note: the PBR shading divides diffuse by π and
    // conserves energy, so intensities are larger than the Blinn-Phong era to land at
    // a comparable brightness.
    Light sun;
    sun.type      = LightType::Directional;
    sun.direction = {-0.4f, -1.0f, -0.3f};  // the direction the rays TRAVEL
    sun.color     = {1.0f, 1.0f, 1.0f};
    sun.intensity = 3.0f;
    lights_.push_back(sun);

    // Light 1 — a warm POINT light off to the right, pooling orange light on the
    // torus and the floor. A point light falls off with distance (see `range`), so
    // nearby surfaces glow while far ones barely register — unlike the sun.
    Light warm;
    warm.type      = LightType::Point;
    warm.position  = {3.6f, 1.2f, 2.2f};
    warm.color     = {1.0f, 0.45f, 0.15f};
    warm.intensity = 24.0f;
    warm.range     = 9.0f;
    lights_.push_back(warm);

    // Light 2 — a cool blue POINT light that ORBITS the scene (animated in run()),
    // so its colored pool sweeps across every surface and the multi-light effect is
    // unmistakable in motion. Its starting position also reads in the t=0 capture.
    Light cool;
    cool.type      = LightType::Point;
    cool.position  = {-3.6f, 1.3f, 2.2f};
    cool.color     = {0.25f, 0.5f, 1.0f};
    cool.intensity = 24.0f;
    cool.range     = 9.0f;
    lights_.push_back(cool);

    // Light 3 — a SPOT light overhead, aimed down at the hub, casting a soft-edged
    // cone of greenish light on the cubes and a ring on the floor. Cutoffs are
    // stored as COSINES of the cone half-angles; because cos DECREASES with angle,
    // the inner (full-bright) cutoff cosine is LARGER than the outer (zero) one.
    Light spot;
    spot.type           = LightType::Spot;
    spot.position       = {0.0f, 4.5f, 1.0f};
    spot.direction      = {0.0f, -1.0f, -0.15f};  // mostly straight down
    spot.color          = {0.7f, 1.0f, 0.7f};
    spot.intensity      = 40.0f;
    spot.range          = 16.0f;
    spot.innerCutoffCos = std::cos(radians(14.0f));
    spot.outerCutoffCos = std::cos(radians(22.0f));
    lights_.push_back(spot);

    KOI_INFO("Lights: 1 directional sun (shadow caster) + 2 point + 1 spot.");
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
        if (renderer_->captureFrame(capturePath, kClearColor, camera_.viewMatrix(),
                                    *sceneRoot_, camera_.position(), lights_, post_)) {
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
        KOI_INFO("Post-processing: 1=tone-map 2=bloom 3=FXAA 4=vignette, [ / ] exposure.");
        KOI_INFO("Lights: 5=point lights 6=spot 7=sun.");
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

        // Orbit the cool point light (index 2) around the scene so its colored pool
        // sweeps across the surfaces — the clearest way to SEE a moving light.
        lightOrbit_ += 0.8f * dt;
        if (lights_.size() > 2) {
            lights_[2].position = {3.6f * std::cos(lightOrbit_), 1.3f,
                                   3.6f * std::sin(lightOrbit_)};
        }

        // Propagate transforms down the tree (parent → child → grandchild) so every
        // node's cached world matrix is current, THEN draw.
        sceneRoot_->updateWorldTransforms();
        renderer_->renderFrame(kClearColor, camera_.viewMatrix(), *sceneRoot_,
                               camera_.position(), lights_, post_);

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
                // In SDL3, event.key.key holds the virtual keycode. ESC quits; the
                // number keys toggle each post-processing effect and the brackets
                // nudge exposure, so the reader can watch what every effect does.
                switch (event.key.key) {
                    case SDLK_ESCAPE:
                        KOI_INFO("Escape pressed — shutting down.");
                        running_ = false;
                        break;
                    case SDLK_1:
                        post_.tonemap = !post_.tonemap;
                        KOI_INFO("Tone-mapping: %s", post_.tonemap ? "on" : "off");
                        break;
                    case SDLK_2:
                        post_.bloom = !post_.bloom;
                        KOI_INFO("Bloom: %s", post_.bloom ? "on" : "off");
                        break;
                    case SDLK_3:
                        post_.fxaa = !post_.fxaa;
                        KOI_INFO("FXAA: %s", post_.fxaa ? "on" : "off");
                        break;
                    case SDLK_4:
                        post_.vignette = !post_.vignette;
                        KOI_INFO("Vignette: %s", post_.vignette ? "on" : "off");
                        break;
                    case SDLK_LEFTBRACKET:  // '[' — darker
                        post_.exposure = SDL_max(0.1f, post_.exposure / 1.25f);
                        KOI_INFO("Exposure: %.2f", post_.exposure);
                        break;
                    case SDLK_RIGHTBRACKET:  // ']' — brighter
                        post_.exposure = SDL_min(8.0f, post_.exposure * 1.25f);
                        KOI_INFO("Exposure: %.2f", post_.exposure);
                        break;
                    case SDLK_5: {
                        // Toggle the two colored POINT lights together (indices 1-2).
                        const bool on = (lights_.size() > 1) ? !lights_[1].enabled : false;
                        if (lights_.size() > 1) {
                            lights_[1].enabled = on;
                        }
                        if (lights_.size() > 2) {
                            lights_[2].enabled = on;
                        }
                        KOI_INFO("Point lights: %s", on ? "on" : "off");
                        break;
                    }
                    case SDLK_6:
                        // Toggle the SPOT light (index 3).
                        if (lights_.size() > 3) {
                            lights_[3].enabled = !lights_[3].enabled;
                            KOI_INFO("Spot light: %s", lights_[3].enabled ? "on" : "off");
                        }
                        break;
                    case SDLK_7:
                        // Toggle the directional SUN (index 0). With it off, the
                        // point/spot lights (which cast no shadow) light the scene alone.
                        if (!lights_.empty()) {
                            lights_[0].enabled = !lights_[0].enabled;
                            KOI_INFO("Sun: %s", lights_[0].enabled ? "on" : "off");
                        }
                        break;
                    default:
                        break;
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

    // Destroy in reverse order of creation. The ORDER HERE MATTERS: the scene holds
    // GPU resources — the meshes' buffers AND the materials' textures — freed
    // through the renderer's device, so it must die BEFORE the renderer destroys
    // that device. (Resetting a smart pointer runs the held object's destructor.)
    sceneRoot_.reset();      // frees all nodes, meshes, and materials' textures
    hub_     = nullptr;      // dangling now that the tree is gone
    spinner_ = nullptr;
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
