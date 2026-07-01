// ============================================================================
//  PostProcess.hpp — post-processing settings + pure helper math (Step 10)
// ----------------------------------------------------------------------------
//  Step 10 renders the scene into an off-screen HDR target and then runs a chain
//  of fullscreen passes over it (bloom, tone-mapping, FXAA). This header holds the
//  small, GPU-free pieces of that feature:
//
//    * PostSettings — the runtime-tunable knobs (exposure, bloom, which effects are
//      on). The Engine owns one, toggles it from input, and hands it to the renderer
//      each frame; the renderer pushes the values into the shaders' uniforms.
//    * A few PURE helpers that mirror the shader math (kept here so they can be
//      unit-tested headlessly and documented in one place — the shaders remain the
//      runtime source of truth).
//
//  Deliberately SDL-free (plain <cstdint>) so it's cheap to include anywhere —
//  Engine.hpp, GpuRenderer.hpp, and the tests all pull it in.
// ============================================================================
#pragma once

#include <algorithm>  // std::max, std::clamp
#include <cstdint>

namespace koi {

// The post-processing knobs, each independently toggleable so the tutorial reader
// can switch effects on/off at runtime and watch what each one contributes. Defaults
// give a pleasant, fully-enabled look.
struct PostSettings {
    float exposure       = 1.0f;   // camera-like brightness multiplier (before tone-map)
    float bloomThreshold = 0.9f;   // luminance above which pixels start to glow
    float bloomIntensity = 1.0f;   // how strongly the blurred glow is added back
    bool  tonemap  = true;         // ACES HDR->LDR curve (vs. a plain clamp)
    bool  bloom    = true;         // the bright-pass + blur + add glow
    bool  fxaa     = true;         // screen-space anti-aliasing (final pass)
    bool  vignette = true;         // subtle darkening toward the corners
};

// Half an extent for the half-resolution bloom targets, clamped to at least 1: a
// blur is forgiving of lost detail, so bloom runs at half-res to save fill, but a
// 1-pixel window must still yield a valid 1x1 texture (never 0).
[[nodiscard]] inline std::uint32_t halfExtent(std::uint32_t x) {
    return std::max<std::uint32_t>(1u, x / 2u);
}

// Perceived brightness (Rec. 709 luma) — mirror of the weights used in bright.frag.
[[nodiscard]] inline float luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ACES filmic tone-map (Narkowicz 2015 fit) for a single channel — the CPU mirror of
// composite.frag's acesToneMap. Compresses unbounded HDR into [0,1]: 0 maps to 0, and
// arbitrarily large inputs saturate just below 1 instead of clipping flat.
[[nodiscard]] inline float acesToneMap(float x) {
    constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    const float y = (x * (a * x + b)) / (x * (c * x + d) + e);
    return std::clamp(y, 0.0f, 1.0f);
}

}  // namespace koi
