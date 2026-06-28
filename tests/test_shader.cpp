// ============================================================================
//  test_shader.cpp — tests for the pure shader-variant selection logic
// ----------------------------------------------------------------------------
//  selectShaderVariant() decides which compiled shader file (and entry point) to
//  load for a given backend, with no GPU or filesystem involved. That makes it a
//  perfect unit-test target: we can feed it format bitmasks and assert the
//  result on any machine, headless CI included.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp already provides
//  the single main() for the whole test binary.
// ============================================================================
#include <doctest/doctest.h>

#include <cstring>

#include "renderer/Shader.hpp"

using koi::selectShaderVariant;

TEST_CASE("selectShaderVariant picks SPIR-V for a Vulkan backend") {
    const koi::ShaderVariant v = selectShaderVariant(SDL_GPU_SHADERFORMAT_SPIRV);
    CHECK(v.valid);
    CHECK(v.format == SDL_GPU_SHADERFORMAT_SPIRV);
    CHECK(std::strcmp(v.extension, ".spv") == 0);
    CHECK(std::strcmp(v.entrypoint, "main") == 0);
}

TEST_CASE("selectShaderVariant picks MSL (entry 'main0') for a Metal backend") {
    const koi::ShaderVariant v = selectShaderVariant(SDL_GPU_SHADERFORMAT_MSL);
    CHECK(v.valid);
    CHECK(v.format == SDL_GPU_SHADERFORMAT_MSL);
    CHECK(std::strcmp(v.extension, ".msl") == 0);
    // spirv-cross renames the MSL entry point to main0 — must match here.
    CHECK(std::strcmp(v.entrypoint, "main0") == 0);
}

TEST_CASE("selectShaderVariant picks DXIL for a Direct3D 12 backend") {
    const koi::ShaderVariant v = selectShaderVariant(SDL_GPU_SHADERFORMAT_DXIL);
    CHECK(v.valid);
    CHECK(v.format == SDL_GPU_SHADERFORMAT_DXIL);
    CHECK(std::strcmp(v.extension, ".dxil") == 0);
}

TEST_CASE("selectShaderVariant reports invalid when no known format is supported") {
    const koi::ShaderVariant v = selectShaderVariant(SDL_GPU_SHADERFORMAT_INVALID);
    CHECK_FALSE(v.valid);
}

TEST_CASE("selectShaderVariant prefers SPIR-V when several formats are offered") {
    // A device advertising multiple formats should resolve deterministically.
    const SDL_GPUShaderFormat both = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
    const koi::ShaderVariant v = selectShaderVariant(both);
    CHECK(v.valid);
    CHECK(v.format == SDL_GPU_SHADERFORMAT_SPIRV);
}
