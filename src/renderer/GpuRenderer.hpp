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
    // clear it to `clearColor`, draw the quad, and present it.
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

    // Create the vertex + index buffers and upload the quad's data into them.
    // Called once from the constructor, after the pipeline. Leaves the buffers
    // null on failure so isValid() reports it.
    bool createGeometry();

    // Create one GPU buffer of `usage` (VERTEX or INDEX), upload `size` bytes
    // from `data` into it via a staging transfer buffer + copy pass, and return
    // it. Returns nullptr (after logging) on failure. The shared helper behind
    // both the vertex and index uploads.
    SDL_GPUBuffer* uploadToGpuBuffer(SDL_GPUBufferUsageFlags usage,
                                     const void* data, Uint32 size);

    // Record the draw for our quad into an already-begun render pass: bind the
    // pipeline + vertex/index buffers and issue the indexed draw. Shared by the
    // live (renderFrame) and off-screen (captureFrame) paths so they can't drift.
    void recordQuad(SDL_GPURenderPass* pass) const;

    SDL_Window*    window_ = nullptr;  // not owned — the Engine owns the Window
    SDL_GPUDevice* device_ = nullptr;  // owned — released in the destructor

    // The graphics pipeline that draws our quad: it bundles the vertex +
    // fragment shaders together with fixed-function state (primitive type, the
    // color target's format, the vertex input layout, etc.) into one object the
    // GPU can switch to quickly.
    SDL_GPUGraphicsPipeline* trianglePipeline_ = nullptr;  // owned

    // The geometry, now living in GPU memory instead of being baked into the
    // shader. The vertex buffer holds the 4 unique corners; the index buffer
    // holds the 6 indices that stitch them into 2 triangles (the whole point of
    // an index buffer: reuse vertices instead of duplicating them).
    SDL_GPUBuffer* vertexBuffer_ = nullptr;  // owned
    SDL_GPUBuffer* indexBuffer_  = nullptr;  // owned
    Uint32         indexCount_   = 0;        // how many indices to draw
};

}  // namespace koi
