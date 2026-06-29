#include "renderer/Shader.hpp"

#include <string>

#include "core/Log.hpp"

namespace koi {

ShaderVariant selectShaderVariant(SDL_GPUShaderFormat supportedFormats) {
    // Prefer SPIR-V where available (Vulkan), then MSL (Metal), then DXIL (D3D12).
    // The order is just a preference; a given backend supports exactly one of
    // these, so at most one branch matches.
    if (supportedFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
        return {".spv", "main", SDL_GPU_SHADERFORMAT_SPIRV, true};
    }
    if (supportedFormats & SDL_GPU_SHADERFORMAT_MSL) {
        // spirv-cross renames the entry point to "main0" when emitting MSL.
        return {".msl", "main0", SDL_GPU_SHADERFORMAT_MSL, true};
    }
    if (supportedFormats & SDL_GPU_SHADERFORMAT_DXIL) {
        return {".dxil", "main", SDL_GPU_SHADERFORMAT_DXIL, true};
    }
    // No format we know how to produce — leave .valid = false for the caller.
    return {"", "", SDL_GPU_SHADERFORMAT_INVALID, false};
}

SDL_GPUShader* loadShader(SDL_GPUDevice* device, const char* name,
                          SDL_GPUShaderStage stage, Uint32 numUniformBuffers) {
    // Ask the device which shader format(s) it accepts, then pick the matching
    // compiled file.
    const ShaderVariant variant = selectShaderVariant(SDL_GetGPUShaderFormats(device));
    if (!variant.valid) {
        KOI_ERROR("No supported shader format for device while loading '%s'", name);
        return nullptr;
    }

    // Compiled shaders live in a "shaders/" folder beside the executable.
    // SDL_GetBasePath() returns that directory (SDL owns the string — don't free).
    const char* basePath = SDL_GetBasePath();
    const std::string path =
        std::string(basePath ? basePath : "") + "shaders/" + name + variant.extension;

    // Read the whole compiled file into memory. SDL_LoadFile null-terminates and
    // reports the byte count via codeSize.
    size_t codeSize = 0;
    void* code = SDL_LoadFile(path.c_str(), &codeSize);
    if (code == nullptr) {
        KOI_ERROR("Failed to load shader file '%s': %s", path.c_str(), SDL_GetError());
        return nullptr;
    }

    // Describe the shader to SDL. The resource counts must match what the
    // compiled shader actually declares; SDL validates them. Only uniform buffers
    // are used so far (the MVP matrix from Step 3) — samplers/storage arrive later.
    SDL_GPUShaderCreateInfo info = {};
    info.code                = static_cast<const Uint8*>(code);
    info.code_size           = codeSize;
    info.entrypoint          = variant.entrypoint;
    info.format              = variant.format;
    info.stage               = stage;
    info.num_samplers        = 0;
    info.num_uniform_buffers = numUniformBuffers;
    info.num_storage_buffers = 0;
    info.num_storage_textures = 0;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    SDL_free(code);  // SDL copied what it needs; release our file buffer

    if (shader == nullptr) {
        KOI_ERROR("SDL_CreateGPUShader failed for '%s': %s", name, SDL_GetError());
        return nullptr;
    }

    KOI_DEBUG("Loaded shader '%s%s' (entry '%s')", name, variant.extension,
              variant.entrypoint);
    return shader;
}

}  // namespace koi
