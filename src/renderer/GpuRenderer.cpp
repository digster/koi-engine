#include "renderer/GpuRenderer.hpp"

#include <array>    // std::array for our compile-time geometry
#include <cstddef>  // offsetof
#include <cstring>  // std::memcpy

#include "core/Log.hpp"
#include "renderer/Shader.hpp"
#include "renderer/Vertex.hpp"

namespace koi {

SDL_PixelFormat gpuColorFormatToPixelFormat(SDL_GPUTextureFormat format) {
    // SDL's *_32 pixel formats are byte-order-defined aliases, so they line up
    // directly with the GPU formats' channel byte order regardless of endianness.
    switch (format) {
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return SDL_PIXELFORMAT_BGRA32;
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return SDL_PIXELFORMAT_RGBA32;
        default:                                   return SDL_PIXELFORMAT_UNKNOWN;
    }
}

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
    //  3. Build the graphics pipeline that draws our quad. If this fails
    //     (e.g. compiled shaders missing) isValid() stays false and the Engine
    //     reports it.
    // ------------------------------------------------------------------------
    if (!createTrianglePipeline()) {
        return;
    }
    KOI_INFO("Graphics pipeline ready.");

    // ------------------------------------------------------------------------
    //  4. Upload our geometry (the quad) into GPU buffers. If this fails,
    //     isValid() stays false and the Engine reports it.
    // ------------------------------------------------------------------------
    if (!createGeometry()) {
        return;
    }
    KOI_INFO("Geometry uploaded (vertex + index buffers ready).");
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

    // ------------------------------------------------------------------------
    //  Vertex input layout — how the GPU reads one vertex out of the buffer.
    //  This has two halves:
    //    * A *buffer description* per bound vertex buffer: its slot (we use 0),
    //      its `pitch` (bytes from one vertex to the next = sizeof(Vertex)), and
    //      whether it advances per-vertex or per-instance (per-vertex here).
    //    * One *attribute* per shader input: which `location` it feeds, which
    //      buffer slot it comes from, its `format` (element type + count), and
    //      its byte `offset` within the vertex. These MUST match koi::Vertex and
    //      the `layout(location=N) in` declarations in triangle.vert.
    // ------------------------------------------------------------------------
    SDL_GPUVertexBufferDescription vertexBufferDesc = {};
    vertexBufferDesc.slot       = 0;
    vertexBufferDesc.pitch      = static_cast<Uint32>(sizeof(Vertex));
    vertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    const SDL_GPUVertexAttribute vertexAttributes[2] = {
        // location 0: inPosition (vec2) at the start of the vertex.
        { /*location=*/0, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          offsetof(Vertex, position) },
        // location 1: inColor (vec3) right after the 2-float position.
        { /*location=*/1, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          offsetof(Vertex, color) },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.vertex_shader   = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
    pipelineInfo.vertex_input_state.num_vertex_buffers         = 1;
    pipelineInfo.vertex_input_state.vertex_attributes          = vertexAttributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes      = 2;
    // TRIANGLELIST = every 3 vertices form one independent triangle.
    pipelineInfo.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.num_color_targets         = 1;
    pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;
    // Everything else (blending, culling, depth) stays zero-initialized: no
    // blending, no face culling, no depth test — exactly what a flat quad
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

SDL_GPUBuffer* GpuRenderer::uploadToGpuBuffer(SDL_GPUBufferUsageFlags usage,
                                              const void* data, Uint32 size) {
    // ------------------------------------------------------------------------
    //  Getting data onto the GPU is a TWO-STEP move, and understanding why is
    //  the heart of this step. The fast memory a vertex/index buffer lives in is
    //  GPU-local and usually NOT directly writable by the CPU. So we:
    //    1. Create the real GPU buffer (the destination — fast, GPU-only).
    //    2. Create a *transfer buffer* (a.k.a. staging buffer): CPU-visible
    //       memory we CAN map and write into.
    //    3. memcpy our data into the mapped transfer buffer.
    //    4. Record a *copy pass* that the GPU runs to move the bytes from the
    //       transfer buffer into the real buffer.
    //  (captureFrame does the mirror image of this to read pixels BACK.)
    // ------------------------------------------------------------------------

    // 1. The destination: a GPU-local buffer of the requested usage.
    SDL_GPUBufferCreateInfo bufInfo = {};
    bufInfo.usage = usage;
    bufInfo.size  = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device_, &bufInfo);
    if (buffer == nullptr) {
        KOI_ERROR("uploadToGpuBuffer: failed to create buffer: %s", SDL_GetError());
        return nullptr;
    }

    // 2. The staging area: a CPU-visible UPLOAD transfer buffer.
    SDL_GPUTransferBufferCreateInfo tbInfo = {};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size  = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tbInfo);
    if (transfer == nullptr) {
        KOI_ERROR("uploadToGpuBuffer: failed to create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUBuffer(device_, buffer);
        return nullptr;
    }

    // 3. Map the staging buffer into our address space, copy the bytes in, unmap.
    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, /*cycle=*/false);
    if (mapped == nullptr) {
        KOI_ERROR("uploadToGpuBuffer: failed to map transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUBuffer(device_, buffer);
        return nullptr;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    // 4. Record + submit a copy pass that performs the staging -> GPU transfer.
    SDL_GPUCommandBuffer* cmd  = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass*      copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = transfer;
    src.offset          = 0;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = buffer;
    dst.offset = 0;
    dst.size   = size;
    SDL_UploadToGPUBuffer(copy, &src, &dst, /*cycle=*/false);
    SDL_EndGPUCopyPass(copy);

    // We don't need a fence here: this is a one-time init upload, and command
    // buffers execute in submission order, so the later per-frame draw is
    // guaranteed to see the finished copy. Releasing the transfer buffer now is
    // safe too — SDL defers the actual free until the GPU is done using it.
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return buffer;
}

bool GpuRenderer::createGeometry() {
    // The four unique corners of the quad, in NDC. With an index buffer we store
    // each corner ONCE; the indices below reference them to build two triangles.
    // (Without indices we'd need to repeat the two shared corners — 6 vertices
    // instead of 4. That saving is exactly why index buffers exist, and it grows
    // dramatically for real meshes where most vertices are shared by many faces.)
    //
    //   3 ───────── 2     positions span -0.5..+0.5 so the quad sits centered;
    //   │  ╲        │      colors are one per corner so the rasterizer blends a
    //   │     ╲     │      smooth red/green/blue/yellow gradient across the face.
    //   │        ╲  │
    //   0 ───────── 1
    constexpr std::array<Vertex, 4> vertices = {{
        { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },  // 0: bottom-left  (red)
        { {  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },  // 1: bottom-right (green)
        { {  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },  // 2: top-right    (blue)
        { { -0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f } },  // 3: top-left     (yellow)
    }};

    // Two triangles (0,1,2) and (2,3,0) tile the quad. Note corners 0 and 2 are
    // each used twice — the reuse the index buffer is built to exploit.
    constexpr std::array<Uint16, 6> indices = { 0, 1, 2, 2, 3, 0 };

    vertexBuffer_ = uploadToGpuBuffer(SDL_GPU_BUFFERUSAGE_VERTEX,
                                      vertices.data(),
                                      static_cast<Uint32>(sizeof(vertices)));
    indexBuffer_  = uploadToGpuBuffer(SDL_GPU_BUFFERUSAGE_INDEX,
                                      indices.data(),
                                      static_cast<Uint32>(sizeof(indices)));
    indexCount_   = static_cast<Uint32>(indices.size());

    return vertexBuffer_ != nullptr && indexBuffer_ != nullptr;
}

void GpuRenderer::recordQuad(SDL_GPURenderPass* pass) const {
    // Switch the GPU to our pipeline, then point it at the geometry buffers.
    SDL_BindGPUGraphicsPipeline(pass, trianglePipeline_);

    // Bind the vertex buffer into slot 0 (the slot our pipeline's vertex input
    // layout describes). offset 0 = start reading from the buffer's beginning.
    SDL_GPUBufferBinding vertexBinding = {};
    vertexBinding.buffer = vertexBuffer_;
    vertexBinding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vertexBinding, /*num_bindings=*/1);

    // Bind the index buffer. Our indices are Uint16, so the element size is 16-bit
    // (use 32-bit only when a mesh has more than 65,536 unique vertices).
    SDL_GPUBufferBinding indexBinding = {};
    indexBinding.buffer = indexBuffer_;
    indexBinding.offset = 0;
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // Indexed draw: for each of the `indexCount_` indices, the GPU looks up that
    // entry in the index buffer and fetches the corresponding vertex. This is
    // what turns our 4 stored vertices into 2 triangles (6 vertex fetches).
    SDL_DrawGPUIndexedPrimitives(pass, indexCount_, /*num_instances=*/1,
                                 /*first_index=*/0, /*vertex_offset=*/0,
                                 /*first_instance=*/0);
}

GpuRenderer::~GpuRenderer() {
    if (device_ != nullptr) {
        // Release GPU resources we created (geometry buffers + pipeline) before
        // tearing down the device that owns them.
        if (vertexBuffer_ != nullptr) {
            SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
            vertexBuffer_ = nullptr;
        }
        if (indexBuffer_ != nullptr) {
            SDL_ReleaseGPUBuffer(device_, indexBuffer_);
            indexBuffer_ = nullptr;
        }
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
        //     The clear gives us the dark-teal background; the quad is drawn
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

        // Draw the quad from its GPU buffers (bind pipeline + vertex/index
        // buffers + indexed draw). Shared with captureFrame so both paths match.
        recordQuad(pass);

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

bool GpuRenderer::captureFrame(const char* path, const SDL_FColor& clearColor) {
    // Fixed capture resolution. Our quad is defined in NDC, so it looks the
    // same at any size; a fixed size keeps the output deterministic and small.
    constexpr Uint32 kWidth  = 1280;
    constexpr Uint32 kHeight = 720;

    // Render into the same format as the swapchain so the existing pipeline (which
    // was built for the swapchain format) is compatible with our off-screen target.
    const SDL_GPUTextureFormat texFormat = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    const SDL_PixelFormat pixelFormat = gpuColorFormatToPixelFormat(texFormat);
    if (pixelFormat == SDL_PIXELFORMAT_UNKNOWN) {
        KOI_ERROR("captureFrame: unsupported swapchain format %d", static_cast<int>(texFormat));
        return false;
    }

    // 1. Off-screen color target we render into (instead of the window).
    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type                 = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format               = texFormat;
    texInfo.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    texInfo.width                = kWidth;
    texInfo.height               = kHeight;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels           = 1;
    SDL_GPUTexture* target = SDL_CreateGPUTexture(device_, &texInfo);
    if (target == nullptr) {
        KOI_ERROR("captureFrame: failed to create target texture: %s", SDL_GetError());
        return false;
    }

    // 2. A "download" transfer buffer: CPU-visible memory the GPU copies into, so
    //    we can read the rendered pixels back.
    SDL_GPUTransferBufferCreateInfo tbInfo = {};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbInfo.size  = kWidth * kHeight * 4;  // 4 bytes per pixel (8-bit RGBA/BGRA)
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tbInfo);
    if (transfer == nullptr) {
        KOI_ERROR("captureFrame: failed to create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(device_, target);
        return false;
    }

    // 3. Record: draw the quad into the off-screen texture, then a copy pass
    //    that downloads that texture into the transfer buffer.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);

    SDL_GPUColorTargetInfo colorTarget = {};
    colorTarget.texture     = target;
    colorTarget.clear_color = clearColor;
    colorTarget.load_op     = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op    = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, nullptr);
    recordQuad(pass);
    SDL_EndGPURenderPass(pass);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region = {};
    region.texture = target;
    region.w = kWidth;
    region.h = kHeight;
    region.d = 1;
    SDL_GPUTextureTransferInfo dst = {};
    dst.transfer_buffer = transfer;
    dst.offset          = 0;
    dst.pixels_per_row  = kWidth;   // tightly packed: pitch = width * 4
    dst.rows_per_layer  = kHeight;
    SDL_DownloadFromGPUTexture(copy, &region, &dst);
    SDL_EndGPUCopyPass(copy);

    // 4. Submit and BLOCK until the GPU finishes — unlike a live frame, we need
    //    the result on the CPU right now. A fence lets us wait for completion.
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence != nullptr) {
        SDL_WaitForGPUFences(device_, /*wait_all=*/true, &fence, 1);
        SDL_ReleaseGPUFence(device_, fence);
    }

    // 5. Map the downloaded pixels, wrap them in a surface, and save as BMP.
    bool ok = false;
    void* pixels = SDL_MapGPUTransferBuffer(device_, transfer, /*cycle=*/false);
    if (pixels != nullptr) {
        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            static_cast<int>(kWidth), static_cast<int>(kHeight),
            pixelFormat, pixels, static_cast<int>(kWidth * 4));
        if (surface != nullptr) {
            ok = SDL_SaveBMP(surface, path);
            if (!ok) KOI_ERROR("captureFrame: SDL_SaveBMP failed: %s", SDL_GetError());
            SDL_DestroySurface(surface);
        }
        SDL_UnmapGPUTransferBuffer(device_, transfer);
    } else {
        KOI_ERROR("captureFrame: failed to map transfer buffer: %s", SDL_GetError());
    }

    // 6. Release the GPU resources we created just for this capture.
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    SDL_ReleaseGPUTexture(device_, target);
    return ok;
}

}  // namespace koi
