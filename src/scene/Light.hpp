// ============================================================================
//  Light.hpp — a light source in the scene (Step 11)
// ----------------------------------------------------------------------------
//  Until now the engine lit everything with ONE hardcoded directional "sun".
//  Real scenes mix many lights of different KINDS, and Step 11 introduces them:
//
//    * DIRECTIONAL — parallel rays from infinitely far away (the sun). It has a
//      direction but no position, and its brightness does NOT fall off with
//      distance (every ray is equally strong everywhere).
//    * POINT — a bulb at a POSITION that radiates equally in all directions. Its
//      brightness FALLS OFF with distance (attenuation), so nearby surfaces are
//      lit strongly and far ones barely at all.
//    * SPOT — a point light restricted to a CONE (a flashlight). It adds a
//      position AND a direction, plus an inner/outer cone angle that fades the
//      light out toward the cone's edge.
//
//  This header is deliberately SDL-free (only the hand-rolled math), so it is
//  cheap to include in Engine, the renderer, and the headless tests. Like
//  Material and PostSettings, a Light is plain data the Engine owns and hands to
//  the renderer each frame; the renderer packs the list into a uniform the
//  fragment shader loops over.
//
//  The two brightness helpers below (attenuation, spotFactor) are PURE and mirror
//  the math in triangle.frag exactly, so they can be unit-tested headlessly and
//  documented in one place. The shader stays the runtime source of truth; these
//  are its CPU twin (the same pattern as PostProcess.hpp's acesToneMap).
// ============================================================================
#pragma once

#include <algorithm>  // std::clamp
#include <cmath>      // std::pow
#include <span>       // std::span — a (pointer, length) view for activeLightCount

#include "math/Vec.hpp"  // Vec3 (SDL-free, header-only math)

namespace koi {

// The maximum number of lights the shader's uniform array can hold. It is a
// FIXED size because a uniform buffer's layout must be known at pipeline-build
// time — the shader declares `lights[MAX_LIGHTS]`, so this constant and the
// shader's `#define MAX_LIGHTS` must agree. (Scaling past a small fixed count is
// what deferred / clustered shading solves — a much later concern.)
inline constexpr int MAX_LIGHTS = 8;

enum class LightType {
    Directional = 0,  // the sun: a direction, no position, no falloff
    Point       = 1,  // a bulb: a position, radiates all ways, falls off
    Spot        = 2,  // a flashlight: a position + direction + a cone
};

// A single light. Which fields matter depends on `type` (a directional light
// ignores position/range/cutoffs, etc.), but keeping one flat struct keeps the
// Engine-side setup and the GPU packing simple. Cutoffs are stored as COSINES of
// the cone half-angles (not the angles themselves) because the shader compares
// cosines directly — cheaper than calling acos per fragment. Note cos is a
// DECREASING function of angle, so innerCutoffCos > outerCutoffCos.
struct Light {
    LightType type      = LightType::Point;
    Vec3      position  = {0.0f, 0.0f, 0.0f};   // point/spot: where the light sits
    Vec3      direction = {0.0f, -1.0f, 0.0f};  // directional/spot: where it points
    Vec3      color     = {1.0f, 1.0f, 1.0f};   // linear RGB
    float     intensity = 1.0f;                 // scalar brightness multiplier
    float     range     = 10.0f;                // point/spot: distance at which it hits 0
    float     innerCutoffCos = 1.0f;            // spot: cos(inner half-angle) — full brightness inside
    float     outerCutoffCos = 0.9f;            // spot: cos(outer half-angle) — zero outside
    bool      enabled        = true;            // runtime on/off (toggled from input)
};

// Distance attenuation for a point/spot light: how much a light of "unit" strength
// still reaches a surface `distance` away. This is the windowed inverse-square
// falloff from Karis' "Real Shading in UE4":
//
//     falloff = clamp(1 - (d/range)^4, 0, 1)^2  /  (d^2 + 1)
//
//   * 1/(d^2 + 1) is the physical INVERSE-SQUARE law (light spreads over a sphere
//     whose area grows with d^2), with a +1 so it's a well-behaved ~1 at d=0
//     instead of dividing by zero.
//   * the clamped `1 - (d/range)^4` window smoothly forces the light to EXACTLY 0
//     at `range`. A hard radius means the shader can ignore lights past it — the
//     inverse-square term alone never truly reaches zero, which would be wasteful.
//
// Result is in [0,1], equals ~1 at d=0, and decreases monotonically to 0 at range.
[[nodiscard]] inline float attenuation(float distance, float range) {
    if (range <= 0.0f) {
        return 0.0f;
    }
    const float ratio  = distance / range;
    float       window = std::clamp(1.0f - ratio * ratio * ratio * ratio, 0.0f, 1.0f);
    window *= window;  // square it for a softer approach to zero
    return window / (distance * distance + 1.0f);
}

// Smooth 0..1 Hermite ramp (the shader's built-in `smoothstep`), replicated on the
// CPU so spotFactor below matches the shader bit-for-concept. Returns 0 for
// x <= edge0, 1 for x >= edge1, and an S-curve between. Guards a zero-width edge.
[[nodiscard]] inline float smoothstep01(float edge0, float edge1, float x) {
    if (edge0 == edge1) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Spot-cone falloff. `thetaCos` is the cosine of the angle between the spot's aim
// direction and the direction to the fragment. Because cos DECREASES with angle,
// a fragment INSIDE the tight inner cone has the LARGEST cosine (>= innerCutoffCos
// → full 1), one OUTSIDE the wide outer cone has the smallest (<= outerCutoffCos
// → 0), and one between rides the smooth ramp. This softens the cone's edge
// instead of a hard circle.
[[nodiscard]] inline float spotFactor(float thetaCos, float innerCutoffCos, float outerCutoffCos) {
    return smoothstep01(outerCutoffCos, innerCutoffCos, thetaCos);
}

// How many lights the renderer will actually upload to the shader: the ENABLED
// ones, capped at MAX_LIGHTS (the shader's fixed array size). Mirrors the count
// produced by the packing loop in GpuRenderer::recordScene, so the cap's behaviour
// is pinned down by a headless unit test (the shader stays the runtime truth).
[[nodiscard]] inline int activeLightCount(std::span<const Light> lights) {
    int count = 0;
    for (const Light& l : lights) {
        if (l.enabled && count < MAX_LIGHTS) {
            ++count;
        }
    }
    return count;
}

}  // namespace koi
