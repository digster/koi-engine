// ============================================================================
//  Shader.hpp — load a compiled shader for whatever backend is running
// ----------------------------------------------------------------------------
//  Our shaders are compiled at build time into several formats (SPIR-V for
//  Vulkan, MSL for Metal — see CMakeLists.txt). At runtime we must load the one
//  the active GPU backend actually understands. This header exposes:
//
//    * selectShaderVariant — a PURE function (no GPU, no files) that maps a
//      device's supported shader formats to the right file extension + shader
//      entry-point name. Kept pure so it can be unit-tested headlessly.
//    * loadShader — reads the matching compiled file and creates an
//      SDL_GPUShader from it.
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

namespace koi {

// Describes which compiled shader file to load for a given backend.
struct ShaderVariant {
    const char*        extension;    // file suffix, e.g. ".spv" or ".msl"
    const char*        entrypoint;   // entry function name in that file
    SDL_GPUShaderFormat format;      // the matching SDL_GPU_SHADERFORMAT_* flag
    bool               valid;        // false if no known format was supported
};

// Pick the shader variant to load given the set of formats a device supports
// (as returned by SDL_GetGPUShaderFormats). Pure and side-effect free.
//
// Note the entry-point quirk: spirv-cross renames `main` to `main0` when it
// emits MSL, so the Metal variant must be loaded with entry point "main0".
[[nodiscard]] ShaderVariant selectShaderVariant(SDL_GPUShaderFormat supportedFormats);

// Load a compiled shader named like "triangle.vert" (no extension) for `stage`,
// choosing the variant the device supports and reading it from the `shaders/`
// folder next to the executable. Returns nullptr (after logging) on failure.
[[nodiscard]] SDL_GPUShader* loadShader(SDL_GPUDevice* device,
                                        const char* name,
                                        SDL_GPUShaderStage stage);

}  // namespace koi
