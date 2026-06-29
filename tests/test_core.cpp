// ============================================================================
//  test_core.cpp — Koi Engine test runner
// ----------------------------------------------------------------------------
//  We use doctest, a single-header C++ test framework. This one macro tells
//  doctest to generate a main() for us, so this file is a complete test
//  executable on its own.
//
//  Why so few tests in Step 0? The interesting, easily-unit-tested logic
//  (vectors, matrices, projections) arrives with the hand-rolled math library
//  in Step 3 — that's where this file will fill out. For now its job is to
//  prove the test target builds against the engine library and runs green
//  under `ctest`, establishing the testing pattern early.
//
//  These tests are intentionally "headless": they never call SDL_Init or open a
//  window, so they pass on a machine with no display (e.g. CI).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/Engine.hpp"
#include "renderer/GpuRenderer.hpp"

TEST_CASE("Engine::Config has sensible defaults") {
    const koi::Engine::Config config{};

    CHECK(config.title != nullptr);
    CHECK(config.width  == 1280);
    CHECK(config.height == 720);
}

TEST_CASE("Engine::Config is overridable via designated initializers") {
    const koi::Engine::Config config{.title = "Test", .width = 640, .height = 480};

    CHECK(config.width  == 640);
    CHECK(config.height == 480);
}

TEST_CASE("gpuColorFormatToPixelFormat maps the formats we download") {
    // The two 8-bit RGBA-ish color formats backends actually use map to byte-order
    // SDL pixel formats; anything else is reported as UNKNOWN so capture bails out.
    CHECK(koi::gpuColorFormatToPixelFormat(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)
          == SDL_PIXELFORMAT_BGRA32);
    CHECK(koi::gpuColorFormatToPixelFormat(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
          == SDL_PIXELFORMAT_RGBA32);
    CHECK(koi::gpuColorFormatToPixelFormat(SDL_GPU_TEXTUREFORMAT_D16_UNORM)
          == SDL_PIXELFORMAT_UNKNOWN);
}
