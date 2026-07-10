// ============================================================================
//  FrameView.hpp — everything the renderer needs to draw one frame
// ----------------------------------------------------------------------------
//  A FrameView is a *snapshot* of what to draw: the camera's view, the scene to
//  traverse, where the eye is, which lights shade it, and how to post-process
//  the result. It is the single value that crosses the engine/app boundary each
//  frame — the APP produces it (from its own scene, camera and settings) and the
//  ENGINE hands it straight to the renderer.
//
//  Why bundle these six things? Both render paths — the live `renderFrame` (to
//  the window) and the headless `captureFrame` (to a BMP, our KOI_CAPTURE
//  debug tool) — need the exact same inputs. Passing them as one struct means
//  the two paths can't drift apart, and a new field (say, a second camera) is
//  added in one place, not threaded through several call sites.
//
//  This struct owns nothing: `root` points at the app's scene graph and `lights`
//  spans the app's light container. Both must outlive the FrameView, which is
//  only ever used within the frame that produced it.
// ============================================================================
#pragma once

#include <SDL3/SDL_pixels.h>  // SDL_FColor — the clear colour (RGBA floats in [0,1])

#include <span>  // std::span — a (pointer, length) view over the app's lights

#include "math/Mat4.hpp"  // Mat4 view matrix (transitively brings in Vec)
#include "math/Vec.hpp"   // Vec3 camera position
#include "renderer/DebugDraw.hpp"    // DebugVertex — the immediate-mode debug lines (Step 22)
#include "renderer/Hud.hpp"          // HudVertex — the 2D screen-space overlay (Step 23)
#include "renderer/PostProcess.hpp"  // PostSettings — which post effects to run
#include "scene/Light.hpp"           // Light — the scene's active light list

namespace koi {

class Node;  // the scene-graph root the renderer traverses; scene/Node.hpp

// One frame's worth of "what to draw", produced by an Application and consumed
// by GpuRenderer::renderFrame / captureFrame. A plain aggregate — designated
// initializers keep call sites readable.
struct FrameView {
    SDL_FColor clearColor{};  // background colour for pixels the scene doesn't cover

    Mat4 view{};  // world -> view (camera) transform; see Camera::viewMatrix()

    // The scene graph to draw. Non-owning and must be non-null: the renderer
    // dereferences it to walk the node tree. Owned by the app.
    const Node* root = nullptr;

    Vec3 cameraPos{};  // eye position in world space — feeds specular highlights

    // The active lights (Step 11); by convention index 0 is the directional sun,
    // the sole shadow caster. A view over app-owned storage (kept alive by the app).
    std::span<const Light> lights{};

    PostSettings post{};  // exposure, tone-map/bloom/FXAA/vignette toggles (Step 10)

    // Immediate-mode debug lines to overlay this frame (Step 22): a line list of
    // world-space, coloured vertices the app rebuilt this frame (AABBs, the camera
    // frustum, light icons). Non-owning — a view over the app's DebugDraw storage,
    // which must outlive the FrameView. Empty ⇒ no overlay drawn (nothing changes).
    std::span<const DebugVertex> debugLines{};

    // Immediate-mode HUD geometry to overlay this frame (Step 23): a triangle list
    // of screen-space textured vertices the app rebuilt this frame (text, panels).
    // Drawn LAST, on the final LDR image, so it stays pixel-crisp. Non-owning — a
    // view over the app's Hud storage, which must outlive the FrameView. Empty ⇒ no
    // HUD drawn (the overlay pass is skipped entirely).
    std::span<const HudVertex> hudVertices{};
};

}  // namespace koi
