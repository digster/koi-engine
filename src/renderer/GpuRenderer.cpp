#include "renderer/GpuRenderer.hpp"

#include "core/Log.hpp"
#include "renderer/Shader.hpp"

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

    // ------------------------------------------------------------------------
    //  3. Build the graphics pipeline that draws our triangle. If this fails
    //     (e.g. compiled shaders missing) isValid() stays false and the Engine
    //     reports it.
    // ------------------------------------------------------------------------
    if (!createTrianglePipeline()) {
        return;
    }
    KOI_INFO("Graphics pipeline ready.");
}

bool GpuRenderer::createTrianglePipeline() {
    // Load the two halves of the programmable pipeline. loadShader picks the
    // compiled file matching this backend (MSL on Metal, SPIR-V on Vulkan).
    SDL_GPUShader* vertexShader   = loadShader(device_, "triangle.vert", SDL_GPU_SHADERSTAGE_VERTEX);
    SDL_GPUShader* fragmentShader = loadShader(device_, "triangle.frag", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertexShader == nullptr || fragmentShader == nullptr) {
        // loadShader already logged the cause. Clean up whichever did load.
        if (vertexShader != nullptr)   SDL_ReleaseGPUShader(device_, vertexShader);
        if (fragmentShader != nullptr) SDL_ReleaseGPUShader(device_, fragmentShader);
        return false;
    }

    // A graphics pipeline bakes the shaders together with fixed-function state.
    // We must tell it the pixel format of what we render into; it has to match
    // the swapchain so the colors are written correctly.
    SDL_GPUColorTargetDescription colorTargetDesc = {};
    colorTargetDesc.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.vertex_shader   = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    // TRIANGLELIST = every 3 vertices form one independent triangle.
    pipelineInfo.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.num_color_targets         = 1;
    pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;
    // Everything else (blending, culling, depth) stays zero-initialized: no
    // blending, no face culling, no depth test — exactly what a flat triangle
    // needs. We'll revisit these in later steps.

    trianglePipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);

    // The pipeline copies what it needs from the shaders, so we can release the
    // shader objects immediately — the pipeline keeps working without them.
    SDL_ReleaseGPUShader(device_, vertexShader);
    SDL_ReleaseGPUShader(device_, fragmentShader);

    if (trianglePipeline_ == nullptr) {
        KOI_ERROR("Failed to create graphics pipeline: %s", SDL_GetError());
        return false;
    }
    return true;
}

GpuRenderer::~GpuRenderer() {
    if (device_ != nullptr) {
        // Release GPU resources we created (the pipeline) before tearing down
        // the device that owns them.
        if (trianglePipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, trianglePipeline_);
            trianglePipeline_ = nullptr;
        }
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
        //     The clear gives us the dark-teal background; the triangle is drawn
        //     on top of it.
        // --------------------------------------------------------------------
        SDL_GPUColorTargetInfo colorTarget = {};
        colorTarget.texture     = swapchainTexture;
        colorTarget.clear_color = clearColor;
        colorTarget.load_op     = SDL_GPU_LOADOP_CLEAR;
        colorTarget.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass =
            SDL_BeginGPURenderPass(cmd, &colorTarget, /*num_color_targets=*/1,
                                   /*depth_stencil_target_info=*/nullptr);

        // Draw the triangle: switch the GPU to our pipeline, then issue a draw
        // for 3 vertices / 1 instance. There's no vertex buffer to bind — the
        // vertex shader generates the 3 positions from gl_VertexIndex (0,1,2).
        SDL_BindGPUGraphicsPipeline(pass, trianglePipeline_);
        SDL_DrawGPUPrimitives(pass, /*num_vertices=*/3, /*num_instances=*/1,
                              /*first_vertex=*/0, /*first_instance=*/0);

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
