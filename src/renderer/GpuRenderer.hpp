// ============================================================================
//  GpuRenderer.hpp — owns the SDL3 GPU device and renders a frame
// ----------------------------------------------------------------------------
//  This is our window into the graphics card. In Step 0 it does the smallest
//  complete thing a GPU renderer can do: clear the screen to a color every
//  frame. That sounds trivial, but it already exercises the entire modern GPU
//  pipeline (device -> swapchain -> command buffer -> render pass -> submit),
//  which is exactly the plumbing every later feature builds on.
//
//  See docs/01-window-and-render-loop.html for a from-first-principles tour of
//  every concept named below (device, swapchain, command buffer, render pass).
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

#include "math/Mat4.hpp"

namespace koi {

// Map a GPU color-target format to the equivalent SDL surface pixel format, so
// downloaded pixels can be wrapped in an SDL_Surface and saved. Pure function
// (no GPU/IO) → unit-testable. Returns SDL_PIXELFORMAT_UNKNOWN for formats we
// don't handle. Used by GpuRenderer::captureFrame.
[[nodiscard]] SDL_PixelFormat gpuColorFormatToPixelFormat(SDL_GPUTextureFormat format);

class GpuRenderer {
public:
    // Construction creates a GPU "device" (our handle to the graphics card)
    // and binds it to the given window so we can present images to it.
    // On failure, isValid() returns false.
    explicit GpuRenderer(SDL_Window* window);

    // Destruction unbinds the window and destroys the device. SDL waits for the
    // GPU to finish any in-flight work before tearing down, so this is safe.
    ~GpuRenderer();

    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;

    // Valid only once the device, the pipeline, AND the geometry buffers are
    // ready, so the Engine aborts with a clear message if (e.g.) the compiled
    // shaders are missing or an upload failed, rather than running with nothing
    // to draw.
    [[nodiscard]] bool isValid() const {
        return device_ != nullptr && trianglePipeline_ != nullptr &&
               vertexBuffer_ != nullptr && indexBuffer_ != nullptr;
    }

    // Render exactly one frame: acquire an image from the window's swapchain,
    // clear it (and the depth buffer), draw the spinning cube, and present it.
    void renderFrame(const SDL_FColor& clearColor);

    // Render one frame into an OFF-SCREEN texture (not the window), download the
    // pixels back to the CPU, and save them to `path` as a BMP. This is our
    // headless visual-debugging tool: it captures exactly what the engine draws
    // without needing screen-recording access or a visible window. Returns false
    // (after logging) on failure. See docs / CLAUDE.md.
    [[nodiscard]] bool captureFrame(const char* path, const SDL_FColor& clearColor);

private:
    // Build the graphics pipeline (loads + compiles shaders, and describes the
    // vertex input layout, into an immutable pipeline object). Called once from
    // the constructor.
    bool createTrianglePipeline();

    // Create the vertex + index buffers and upload the cube's data into them.
    // Called once from the constructor, after the pipeline. Leaves the buffers
    // null on failure so isValid() reports it.
    bool createGeometry();

    // Create one GPU buffer of `usage` (VERTEX or INDEX), upload `size` bytes
    // from `data` into it via a staging transfer buffer + copy pass, and return
    // it. Returns nullptr (after logging) on failure. The shared helper behind
    // both the vertex and index uploads.
    SDL_GPUBuffer* uploadToGpuBuffer(SDL_GPUBufferUsageFlags usage,
                                     const void* data, Uint32 size);

    // Record the draw for our cube into an already-begun render pass: bind the
    // pipeline + vertex/index buffers and issue the indexed draw. Shared by the
    // live (renderFrame) and off-screen (captureFrame) paths so they can't drift.
    void recordCube(SDL_GPURenderPass* pass) const;

    // Ensure the depth texture exists and matches (w, h), recreating it on resize.
    // Returns false (after logging) if creation fails. Called each frame before
    // the render pass, since the window — and thus the needed depth size — can
    // change. See docs/04 for what a depth buffer is and why we need one.
    bool ensureDepthTexture(Uint32 width, Uint32 height);

    // Build the Model-View-Projection matrix for the cube at the given viewport
    // aspect ratio and rotation angle. View is identity for now (the camera
    // arrives in Step 4); the camera distance is folded into the model translate.
    [[nodiscard]] Mat4 buildMvp(float aspect, float angleRadians) const;

    SDL_Window*    window_ = nullptr;  // not owned — the Engine owns the Window
    SDL_GPUDevice* device_ = nullptr;  // owned — released in the destructor

    // The graphics pipeline that draws our cube: it bundles the vertex +
    // fragment shaders together with fixed-function state (primitive type, the
    // color target's format, the vertex input layout, etc.) into one object the
    // GPU can switch to quickly.
    SDL_GPUGraphicsPipeline* trianglePipeline_ = nullptr;  // owned

    // The geometry, living in GPU memory. The vertex buffer holds the cube's 8
    // unique corners; the index buffer holds 36 indices (12 triangles, 2 per
    // face) that reuse those corners — the index buffer's payoff: 8 vertices, not
    // 36.
    SDL_GPUBuffer* vertexBuffer_ = nullptr;  // owned
    SDL_GPUBuffer* indexBuffer_  = nullptr;  // owned
    Uint32         indexCount_   = 0;        // how many indices to draw

    // The depth buffer: a texture the same size as the color target that stores,
    // per pixel, the depth of the nearest fragment drawn so far. With the depth
    // test enabled, the GPU keeps a new fragment only if it's closer — that's
    // how the cube's near faces correctly hide its far faces, regardless of draw
    // order. Created lazily and resized to match the swapchain.
    SDL_GPUTexture*      depthTexture_ = nullptr;  // owned
    Uint32              depthWidth_   = 0;
    Uint32              depthHeight_  = 0;
    SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;  // chosen at pipeline build
};

}  // namespace koi
