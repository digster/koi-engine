#include "renderer/GpuRenderer.hpp"

#include "core/Log.hpp"

namespace koi {

GpuRenderer::GpuRenderer(SDL_Window* window) : window_(window) {
    // ------------------------------------------------------------------------
    //  1. Create the GPU device.
    //     A "device" is our software handle to the graphics card. Creating it
    //     picks a backend appropriate for the platform — Metal on macOS,
    //     Vulkan on Linux, Direct3D 12 on Windows — without us writing any
    //     backend-specific code.
    //
    //     The first argument is a bitmask of *shader formats* we promise to
    //     supply later. Each backend consumes a different compiled shader
    //     language (Vulkan->SPIR-V, Metal->MSL, D3D12->DXIL). We list all three
    //     so this engine stays cross-platform; SDL picks a backend that matches
    //     one of them. (We have no shaders yet in Step 0 — this just declares
    //     intent for when we add them in Step 1.)
    //
    //     The second argument enables the GPU debug layer: extra validation and
    //     error messages, invaluable while learning. We'll disable it in
    //     Release builds for speed.
    // ------------------------------------------------------------------------
    const SDL_GPUShaderFormat shaderFormats =
        SDL_GPU_SHADERFORMAT_SPIRV |  // Vulkan
        SDL_GPU_SHADERFORMAT_MSL   |  // Metal (macOS)
        SDL_GPU_SHADERFORMAT_DXIL;    // Direct3D 12

    device_ = SDL_CreateGPUDevice(shaderFormats, /*debug_mode=*/true, /*name=*/nullptr);
    if (device_ == nullptr) {
        KOI_ERROR("Failed to create GPU device: %s", SDL_GetError());
        return;
    }

    // ------------------------------------------------------------------------
    //  2. Claim the window for this device.
    //     This creates the window's "swapchain": a small set of images that the
    //     GPU draws into and the OS displays, cycling between them so the screen
    //     never shows a half-drawn frame (double/triple buffering). After this
    //     call, we can acquire one of those images each frame and render to it.
    // ------------------------------------------------------------------------
    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        KOI_ERROR("Failed to claim window for GPU device: %s", SDL_GetError());
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
        return;
    }

    // Log which backend SDL actually chose — great for understanding what's
    // running under the hood (expect "metal" on this Mac).
    KOI_INFO("GPU device ready (backend: %s)", SDL_GetGPUDeviceDriver(device_));
}

GpuRenderer::~GpuRenderer() {
    if (device_ != nullptr) {
        // Order matters: detach the window's swapchain from the device BEFORE
        // destroying the device. DestroyGPUDevice also waits for the GPU to go
        // idle, so no in-flight frame is left dangling.
        if (window_ != nullptr) {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
        }
        SDL_DestroyGPUDevice(device_);
        device_ = nullptr;
    }
}

void GpuRenderer::renderFrame(const SDL_FColor& clearColor) {
    // ------------------------------------------------------------------------
    //  1. Acquire a command buffer.
    //     We don't command the GPU directly; we *record* commands into a buffer
    //     and submit them in bulk. This is the asynchronous heart of modern
    //     graphics: the CPU builds the list, the GPU executes it independently.
    // ------------------------------------------------------------------------
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (cmd == nullptr) {
        KOI_ERROR("Failed to acquire command buffer: %s", SDL_GetError());
        return;
    }

    // ------------------------------------------------------------------------
    //  2. Acquire the swapchain texture (the image we'll draw this frame).
    //     "Wait" means: block until the GPU has finished with one of the
    //     swapchain's images and handed it back to us. This naturally paces our
    //     loop to the display's refresh rather than spinning uselessly.
    // ------------------------------------------------------------------------
    SDL_GPUTexture* swapchainTexture = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swapchainTexture,
                                               /*width=*/nullptr, /*height=*/nullptr)) {
        KOI_ERROR("Failed to acquire swapchain texture: %s", SDL_GetError());
        // Even on failure we must hand the command buffer back, or it leaks.
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    // The call can succeed yet return a null texture when there's nothing to
    // draw into — e.g. the window is minimized. That's not an error: we simply
    // skip the render pass this frame but still submit the (empty) buffer.
    if (swapchainTexture != nullptr) {
        // --------------------------------------------------------------------
        //  3. Describe and begin a render pass.
        //     A render pass declares which texture(s) we're drawing into and
        //     what to do with their existing contents:
        //       load_op  = CLEAR  -> wipe to clear_color before drawing.
        //       store_op = STORE  -> keep the result so it can be displayed.
        //     For Step 0, the clear *is* the entire frame — there are no draw
        //     calls between Begin and End yet.
        // --------------------------------------------------------------------
        SDL_GPUColorTargetInfo colorTarget = {};
        colorTarget.texture     = swapchainTexture;
        colorTarget.clear_color = clearColor;
        colorTarget.load_op     = SDL_GPU_LOADOP_CLEAR;
        colorTarget.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass =
            SDL_BeginGPURenderPass(cmd, &colorTarget, /*num_color_targets=*/1,
                                   /*depth_stencil_target_info=*/nullptr);

        // (Step 1 onward: bind a pipeline and issue draw calls here.)

        SDL_EndGPURenderPass(pass);
    }

    // ------------------------------------------------------------------------
    //  4. Submit.
    //     Hand the recorded command buffer to the GPU for execution and queue
    //     the result for display. We do NOT block waiting for it to finish —
    //     the CPU is free to start building the next frame immediately.
    // ------------------------------------------------------------------------
    SDL_SubmitGPUCommandBuffer(cmd);
}

}  // namespace koi
