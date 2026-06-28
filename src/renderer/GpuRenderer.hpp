// ============================================================================
//  GpuRenderer.hpp — owns the SDL3 GPU device and renders a frame
// ----------------------------------------------------------------------------
//  This is our window into the graphics card. In Step 0 it does the smallest
//  complete thing a GPU renderer can do: clear the screen to a color every
//  frame. That sounds trivial, but it already exercises the entire modern GPU
//  pipeline (device -> swapchain -> command buffer -> render pass -> submit),
//  which is exactly the plumbing every later feature builds on.
//
//  See docs/01-window-and-render-loop.md for a from-first-principles tour of
//  every concept named below (device, swapchain, command buffer, render pass).
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

namespace koi {

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

    [[nodiscard]] bool isValid() const { return device_ != nullptr; }

    // Render exactly one frame: acquire an image from the window's swapchain,
    // clear it to `clearColor`, and present it. In Step 1 this single call will
    // be split into begin/draw/end once we actually have geometry to draw.
    void renderFrame(const SDL_FColor& clearColor);

private:
    SDL_Window*    window_ = nullptr;  // not owned — the Engine owns the Window
    SDL_GPUDevice* device_ = nullptr;  // owned — released in the destructor
};

}  // namespace koi
