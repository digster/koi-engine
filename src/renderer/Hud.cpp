// ============================================================================
//  Hud.cpp — turn text + rectangles into textured overlay vertices (see header)
// ============================================================================
#include "renderer/Hud.hpp"

namespace koi {

void Hud::quad(float x0, float y0, float x1, float y1, const UvRect& uv, const Vec4& color) {
    // Two triangles cover the axis-aligned quad. Winding doesn't matter (the HUD
    // pipeline disables face culling), so we pick the readable TL→TR→BR, TL→BR→BL
    // order. Each screen corner carries the matching atlas corner and the tint.
    const HudVertex tl{Vec2{x0, y0}, Vec2{uv.u0, uv.v0}, color};
    const HudVertex tr{Vec2{x1, y0}, Vec2{uv.u1, uv.v0}, color};
    const HudVertex br{Vec2{x1, y1}, Vec2{uv.u1, uv.v1}, color};
    const HudVertex bl{Vec2{x0, y1}, Vec2{uv.u0, uv.v1}, color};

    verts_.push_back(tl);
    verts_.push_back(tr);
    verts_.push_back(br);
    verts_.push_back(tl);
    verts_.push_back(br);
    verts_.push_back(bl);
}

void Hud::text(float x, float y, float scale, const Vec4& color, std::string_view str) {
    const float advance = static_cast<float>(kGlyphPx) * scale;  // one fixed-width cell
    float penX = x;
    float penY = y;
    for (const char c : str) {
        if (c == '\n') {          // carriage return + line feed in one
            penX = x;
            penY += advance;
            continue;
        }
        // Every printable char (space included) emits one cell-sized quad pointing
        // at its atlas cell; a space's cell is transparent, so it just moves the pen.
        const UvRect uv = cellUV(glyphCell(c));
        quad(penX, penY, penX + advance, penY + advance, uv, color);
        penX += advance;
    }
}

void Hud::rect(float x, float y, float w, float h, const Vec4& color) {
    // Sample the atlas's solid-white cell so a filled panel reuses the text
    // pipeline: white * color = color, with color.a controlling translucency.
    quad(x, y, x + w, y + h, cellUV(kWhiteCell), color);
}

float Hud::textWidth(std::string_view str, float scale) {
    const float advance = static_cast<float>(kGlyphPx) * scale;
    float widest = 0.0f;
    float lineW  = 0.0f;
    for (const char c : str) {
        if (c == '\n') {
            widest = (lineW > widest) ? lineW : widest;
            lineW  = 0.0f;
            continue;
        }
        lineW += advance;
    }
    return (lineW > widest) ? lineW : widest;
}

}  // namespace koi
