// ============================================================================
//  Hud.hpp — immediate-mode 2D overlay: text and filled rectangles
// ----------------------------------------------------------------------------
//  Step 22 gave us debug LINES in world space. This is its 2D sibling: a HUD
//  ("heads-up display") that draws in SCREEN space — text and panels pinned to
//  pixels on the window, never moving with the camera. It's how an engine shows
//  an FPS counter, a controls legend, or a value readout on top of the scene.
//
//  Like DebugDraw, this is IMMEDIATE MODE and PURE. Immediate mode: nothing
//  persists between frames — each frame the app calls clear() then re-declares
//  every string and box it wants, and the collected triangles ARE the frame's
//  HUD. Pure: this file has no SDL and no GPU. It only turns text + rectangles
//  into a flat list of textured vertices, which keeps it fully unit-testable
//  (tests/test_hud.cpp) and leaves the GPU side (atlas upload, the textured
//  overlay pipeline) a thin consumer in GpuRenderer.
//
//  Coordinates are in PIXELS with the ORIGIN AT THE TOP-LEFT (x right, y down) —
//  the natural way to think about a 2D screen. hud.vert converts these pixels to
//  clip space using the viewport size, so callers never touch a matrix.
//
//  Everything is emitted as a TRIANGLE LIST (6 vertices = 2 triangles = 1 quad),
//  and every quad — glyph or panel — samples the SAME font atlas, so the whole
//  HUD draws with one pipeline in a single draw call. Glyphs sample their cell;
//  filled rects sample the atlas's reserved solid-white cell (see Font.hpp).
//  Concepts are explained in documentation/docs/24-hud-and-text.html.
// ============================================================================
#pragma once

#include <span>          // std::span — the (pointer,length) view the renderer uploads
#include <string_view>   // std::string_view — the text to lay out (no allocation)
#include <vector>        // std::vector — the growing per-frame vertex list

#include "math/Vec.hpp"       // Vec2 position/uv, Vec4 RGBA colour
#include "renderer/Font.hpp"  // atlas layout + UvRect (the shared glyph contract)

namespace koi {

// One vertex of the HUD overlay: a screen-space position (pixels, top-left
// origin), an atlas texture coordinate, and an RGBA colour. The textured overlay
// pipeline reads exactly this layout (pos @0, uv @8, colour @16), so field order
// and types are a binding contract with hud.vert.
struct HudVertex {
    Vec2 pos;    // pixel position on screen (x right, y down)
    Vec2 uv;     // atlas texture coordinate in [0,1]
    Vec4 color;  // tint: glyph coverage / white panel is multiplied by this RGBA
};

// Collects the HUD geometry to draw this frame. Two triangles per quad, stored
// as six HudVertex, so the renderer draws them all with one TRIANGLELIST call.
class Hud {
public:
    // Drop last frame's geometry. Keeps the vector's capacity so re-filling each
    // frame costs no allocation once it has grown to its working size.
    void clear() { verts_.clear(); }

    // Lay out a run of text with its top-left corner at (x, y). `scale` multiplies
    // the 8px cell, so scale=2 gives 16px-tall glyphs; the pen advances one cell
    // (8*scale px) per character — a fixed-width font. A '\n' returns the pen to
    // the starting x and drops one line. Unknown characters render as '?' (see
    // Font::glyphCell). Space emits an (invisible) transparent quad, so the vertex
    // count is a predictable 6 per non-newline character.
    void text(float x, float y, float scale, const Vec4& color, std::string_view str);

    // A solid filled rectangle from (x, y) spanning w x h pixels — a HUD panel or
    // background. Samples the atlas's reserved white cell, so it shares the text
    // pipeline; give `color` an alpha < 1 for a translucent backdrop.
    void rect(float x, float y, float w, float h, const Vec4& color);

    // The pixel width a string would occupy at `scale` (handy for right-aligning
    // or sizing a backing panel). Counts the widest line for multi-line input.
    [[nodiscard]] static float textWidth(std::string_view str, float scale);

    // A read-only view of the collected vertices for the renderer to upload. Empty
    // when nothing was queued this frame (the renderer then skips the overlay pass).
    [[nodiscard]] std::span<const HudVertex> vertices() const { return verts_; }
    [[nodiscard]] bool   empty() const { return verts_.empty(); }
    [[nodiscard]] size_t size()  const { return verts_.size(); }

private:
    // Append the two triangles of one screen quad (x0,y0)-(x1,y1) mapped to the
    // atlas rect `uv`, all six vertices sharing `color`. The unit all glyph/panel
    // emission funnels through.
    void quad(float x0, float y0, float x1, float y1, const UvRect& uv, const Vec4& color);

    std::vector<HudVertex> verts_;
};

}  // namespace koi
