// ============================================================================
//  Application.hpp — the contract every Koi app implements
// ----------------------------------------------------------------------------
//  This interface is the engine/app boundary. The ENGINE owns the machinery that
//  every app needs but no app should re-write: the window, the renderer, the SDL
//  lifecycle, the delta-time clock, and the main loop (plus the headless
//  KOI_CAPTURE / KOI_MAX_FRAMES paths). The APP owns everything specific to it:
//  the scene, the camera, the lights, and how they respond to time and input.
//
//  Control is INVERTED. Rather than the engine calling down into hardcoded demo
//  code, the engine calls *back* into these hooks on an object it knows nothing
//  concrete about. That's what lets `koi_core` ship as a reusable library: the
//  demo under samples/ is just one Application; a real game would be another.
//
//  Lifecycle, as driven by Engine::run(app):
//      onStart(engine)          once, after the renderer is ready — build content
//      per frame: onEvent(...)  for each OS/input event this frame
//                 onUpdate(...)  once, with the elapsed seconds — animate + input
//                 frameView()    the engine asks "what do I draw?" and renders it
//      onShutdown()             once, before the engine tears the device down
// ============================================================================
#pragma once

#include <SDL3/SDL_events.h>  // SDL_Event — delivered to onEvent

#include "renderer/FrameView.hpp"  // FrameView — returned by frameView() (by value)

namespace koi {

class Engine;  // passed to the hooks so the app can reach engine services

// Abstract base for a Koi application. Subclass it, hold your scene/camera/lights
// as members, and implement the hooks. The engine owns the loop; you own the world.
class Application {
public:
    virtual ~Application() = default;

    // Called once, after the engine's window + renderer exist. Build the scene
    // graph, load assets (via engine.renderer()), place the camera and lights.
    // Return false (after logging) to abort startup — e.g. a mesh failed to load.
    virtual bool onStart(Engine& engine) = 0;

    // Called once per frame with the time since the previous frame, in seconds.
    // Advance animation and read continuous (held-key) input here. World-transform
    // propagation is the engine's job — it runs right before drawing.
    virtual void onUpdate(Engine& engine, float dt) = 0;

    // Called for each queued OS/input event (keyboard, mouse, window). Handle
    // discrete input here — toggles, mouse-look deltas, and quitting (call
    // engine.requestQuit()). The default ignores everything.
    virtual void onEvent(Engine& engine, const SDL_Event& event) {
        (void)engine;
        (void)event;
    }

    // The engine calls this after onUpdate to learn what to draw this frame, then
    // hands the result to the renderer (live) or the capture tool (headless). The
    // returned FrameView only borrows the app's scene/lights, so it must stay
    // valid for the duration of the call — returning members-by-view is fine.
    [[nodiscard]] virtual FrameView frameView() const = 0;

    // Called once as the loop exits, before the renderer/device are destroyed.
    // Release anything that must die before the GPU device does. Default: nothing.
    virtual void onShutdown() {}
};

}  // namespace koi
