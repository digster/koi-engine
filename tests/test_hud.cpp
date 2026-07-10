// ============================================================================
//  test_hud.cpp — unit tests for the Step 23 HUD / text collector
// ----------------------------------------------------------------------------
//  Hud is pure (no GPU, no window): it only turns text + rectangles into a flat
//  list of textured triangle-list vertices. That makes its layout exactly
//  testable headlessly — a wrong pen advance, a flipped quad winding, or a glyph
//  pointing at the wrong atlas cell would otherwise be a subtle "the text looks
//  off" bug. The GPU side (atlas bake + overlay pipeline) is verified end-to-end
//  by a KOI_HUD frame capture instead.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include "math/Vec.hpp"
#include "renderer/Font.hpp"
#include "renderer/Hud.hpp"

using namespace koi;

static bool vec2Approx(const Vec2& a, const Vec2& b, float eps = 1e-4f) {
    return a.x == doctest::Approx(b.x).epsilon(eps) &&
           a.y == doctest::Approx(b.y).epsilon(eps);
}

static bool vec4Approx(const Vec4& a, const Vec4& b, float eps = 1e-4f) {
    return a.x == doctest::Approx(b.x).epsilon(eps) &&
           a.y == doctest::Approx(b.y).epsilon(eps) &&
           a.z == doctest::Approx(b.z).epsilon(eps) &&
           a.w == doctest::Approx(b.w).epsilon(eps);
}

TEST_CASE("empty string emits no geometry") {
    Hud hud;
    CHECK(hud.empty());
    hud.text(0, 0, 1.0f, {1, 1, 1, 1}, "");
    CHECK(hud.empty());
    CHECK(hud.size() == 0);
}

TEST_CASE("text() emits six vertices per character, including spaces") {
    Hud hud;
    hud.text(0, 0, 1.0f, {1, 1, 1, 1}, "Hi there");  // 8 chars incl. the space
    CHECK(hud.size() == 8 * 6);
}

TEST_CASE("text() places the first glyph quad at (x, y) and advances one cell") {
    Hud hud;
    const float scale = 2.0f;
    const float adv   = static_cast<float>(kGlyphPx) * scale;  // 16px per cell
    hud.text(10, 20, scale, {1, 1, 1, 1}, "Hi");
    REQUIRE(hud.size() == 12);
    const auto v = hud.vertices();

    // Quad vertex order is TL, TR, BR, TL, BR, BL. First glyph 'H' spans (10,20)..(26,36).
    CHECK(vec2Approx(v[0].pos, {10, 20}));           // TL
    CHECK(vec2Approx(v[1].pos, {10 + adv, 20}));     // TR
    CHECK(vec2Approx(v[2].pos, {10 + adv, 20 + adv}));  // BR
    CHECK(vec2Approx(v[5].pos, {10, 20 + adv}));     // BL

    // Second glyph 'i' starts one cell to the right, same row.
    CHECK(vec2Approx(v[6].pos, {10 + adv, 20}));
}

TEST_CASE("text() points each glyph at its atlas cell") {
    Hud hud;
    hud.text(0, 0, 1.0f, {1, 1, 1, 1}, "A");
    REQUIRE(hud.size() == 6);
    const UvRect expect = cellUV(glyphCell('A'));
    const auto v = hud.vertices();
    // TL vertex carries the cell's (u0,v0); BR carries (u1,v1).
    CHECK(vec2Approx(v[0].uv, {expect.u0, expect.v0}));
    CHECK(vec2Approx(v[2].uv, {expect.u1, expect.v1}));
}

TEST_CASE("unknown characters fall back to the '?' glyph") {
    Hud hud;
    hud.text(0, 0, 1.0f, {1, 1, 1, 1}, "\x01");  // a control char, not printable
    REQUIRE(hud.size() == 6);
    const UvRect q = cellUV(glyphCell('?'));
    CHECK(vec2Approx(hud.vertices()[0].uv, {q.u0, q.v0}));
}

TEST_CASE("newline returns the pen to x and drops one line") {
    Hud hud;
    const float scale = 1.0f;
    const float adv   = static_cast<float>(kGlyphPx) * scale;
    hud.text(4, 7, scale, {1, 1, 1, 1}, "A\nB");
    // 'A' (6) + '\n' (0) + 'B' (6) = 12 vertices; the newline itself emits nothing.
    REQUIRE(hud.size() == 12);
    const auto v = hud.vertices();
    CHECK(vec2Approx(v[0].pos, {4, 7}));            // 'A' at the start
    CHECK(vec2Approx(v[6].pos, {4, 7 + adv}));      // 'B' back at x, one line down
}

TEST_CASE("every vertex carries the requested colour") {
    Hud hud;
    const Vec4 color{0.2f, 0.4f, 0.6f, 0.8f};
    hud.text(0, 0, 1.0f, color, "Xy");
    for (const HudVertex& hv : hud.vertices()) {
        CHECK(vec4Approx(hv.color, color));
    }
}

TEST_CASE("rect() emits one white-sampled quad spanning the given box") {
    Hud hud;
    hud.rect(5, 5, 100, 20, {0, 0, 0, 0.5f});
    REQUIRE(hud.size() == 6);
    const auto v = hud.vertices();
    CHECK(vec2Approx(v[0].pos, {5, 5}));        // TL
    CHECK(vec2Approx(v[2].pos, {105, 25}));     // BR

    // A filled rect samples the reserved solid-white cell so it shares the pipeline.
    const UvRect white = cellUV(kWhiteCell);
    for (const HudVertex& hv : v) {
        CHECK(hv.uv.x >= doctest::Approx(white.u0));
        CHECK(hv.uv.x <= doctest::Approx(white.u1));
        CHECK(hv.uv.y >= doctest::Approx(white.v0));
        CHECK(hv.uv.y <= doctest::Approx(white.v1));
    }
}

TEST_CASE("all UVs stay within the atlas [0,1] range") {
    Hud hud;
    hud.text(0, 0, 1.0f, {1, 1, 1, 1}, "Koi 123 !?~");
    hud.rect(0, 0, 10, 10, {1, 1, 1, 1});
    for (const HudVertex& hv : hud.vertices()) {
        CHECK(hv.uv.x >= 0.0f);
        CHECK(hv.uv.x <= 1.0f);
        CHECK(hv.uv.y >= 0.0f);
        CHECK(hv.uv.y <= 1.0f);
    }
}

TEST_CASE("clear() empties the list but is reusable") {
    Hud hud;
    hud.text(0, 0, 1.0f, {1, 1, 1, 1}, "abc");
    CHECK(hud.size() == 18);
    hud.clear();
    CHECK(hud.empty());
    CHECK(hud.size() == 0);
    hud.text(0, 0, 1.0f, {1, 1, 1, 1}, "de");
    CHECK(hud.size() == 12);
}

TEST_CASE("textWidth() measures the widest line") {
    const float scale = 2.0f;
    const float adv   = static_cast<float>(kGlyphPx) * scale;
    CHECK(Hud::textWidth("ABC", scale) == doctest::Approx(3 * adv));
    // Multi-line: the widest of "AB" (2) and "CDE" (3).
    CHECK(Hud::textWidth("AB\nCDE", scale) == doctest::Approx(3 * adv));
    CHECK(Hud::textWidth("", scale) == doctest::Approx(0.0f));
}
