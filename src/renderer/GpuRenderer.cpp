#include "renderer/GpuRenderer.hpp"

#include <cstddef>  // offsetof
#include <cstring>  // std::memcpy
#include <memory>   // std::make_shared
#include <vector>   // std::vector (repack texture rows to a tight pitch)

#include "core/Log.hpp"
#include "math/Mat4.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Shader.hpp"
#include "renderer/Texture.hpp"
#include "renderer/Vertex.hpp"
#include "scene/Node.hpp"

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
    //  3. Build the graphics pipeline that draws our meshes. If this fails
    //     (e.g. compiled shaders missing) isValid() stays false and the Engine
    //     reports it.
    // ------------------------------------------------------------------------
    if (!createTrianglePipeline()) {
        return;
    }
    KOI_INFO("Graphics pipeline ready.");

    // ------------------------------------------------------------------------
    //  4. Create the shared sampler that every texture is read through. If this
    //     fails, isValid() stays false and the Engine reports it.
    // ------------------------------------------------------------------------
    if (!createSampler()) {
        return;
    }

    // Note: no geometry or textures are uploaded here. The renderer is a factory
    // for meshes (createMesh) and textures (loadTexture) — the Engine builds the
    // cube + plane + texture after this constructor returns and binds them.
}

// Pick a depth texture format the device supports as a depth-stencil target.
// We prefer 32-bit float depth (most precise, universally supported on Metal),
// then 24-bit, then 16-bit. Returns INVALID if somehow none work.
static SDL_GPUTextureFormat chooseDepthFormat(SDL_GPUDevice* device) {
    const SDL_GPUTextureFormat candidates[] = {
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    };
    for (const SDL_GPUTextureFormat fmt : candidates) {
        if (SDL_GPUTextureSupportsFormat(device, fmt, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            return fmt;
        }
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool GpuRenderer::createTrianglePipeline() {
    // Load the two halves of the programmable pipeline. loadShader picks the
    // compiled file matching this backend (MSL on Metal, SPIR-V on Vulkan).
    // The vertex shader declares one uniform buffer (the MVP matrix); the
    // fragment shader declares none.
    SDL_GPUShader* vertexShader   = loadShader(device_, "triangle.vert", SDL_GPU_SHADERSTAGE_VERTEX, /*numUniformBuffers=*/1);
    SDL_GPUShader* fragmentShader = loadShader(device_, "triangle.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, /*numUniformBuffers=*/2, /*numSamplers=*/1);
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

    // Choose (once) the depth-buffer format this pipeline — and our per-frame
    // depth textures — will use. The pipeline must know it up front so its depth
    // test is compatible with the depth texture we attach each frame.
    depthFormat_ = chooseDepthFormat(device_);
    if (depthFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID) {
        KOI_ERROR("No supported depth texture format found.");
        SDL_ReleaseGPUShader(device_, vertexShader);
        SDL_ReleaseGPUShader(device_, fragmentShader);
        return false;
    }

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

    const SDL_GPUVertexAttribute vertexAttributes[4] = {
        // location 0: inPosition (vec3) at the start of the vertex.
        { /*location=*/0, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          offsetof(Vertex, position) },
        // location 1: inColor (vec3) right after the 3-float position.
        { /*location=*/1, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          offsetof(Vertex, color) },
        // location 2: inUV (vec2) right after the color — Step 6's texture coords.
        { /*location=*/2, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
          offsetof(Vertex, uv) },
        // location 3: inNormal (vec3) right after the uv — Step 7's surface normal.
        { /*location=*/3, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          offsetof(Vertex, normal) },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.vertex_shader   = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
    pipelineInfo.vertex_input_state.num_vertex_buffers         = 1;
    pipelineInfo.vertex_input_state.vertex_attributes          = vertexAttributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes      = 4;
    // TRIANGLELIST = every 3 vertices form one independent triangle.
    pipelineInfo.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    // Depth testing: enable it so the cube's near faces correctly hide its far
    // faces. compare_op = LESS means "keep the new fragment only if its depth is
    // smaller (closer) than what's already there"; we clear depth to 1.0 (far)
    // each frame and write the winner's depth back. Without this, triangles would
    // simply paint over each other in draw order and the cube would look wrong.
    pipelineInfo.depth_stencil_state.enable_depth_test  = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = true;
    pipelineInfo.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS;

    pipelineInfo.target_info.num_color_targets         = 1;
    pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;
    // Tell the pipeline it renders into a depth target too, and its format.
    pipelineInfo.target_info.has_depth_stencil_target  = true;
    pipelineInfo.target_info.depth_stencil_format      = depthFormat_;
    // Blending and face culling stay at defaults (off): the depth test alone
    // resolves visibility for our opaque cube. Culling is a later optimization.

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

bool GpuRenderer::createSampler() {
    // A SAMPLER is the "how to read a texture" object — entirely separate from any
    // texture's pixel data, which is why one shared sampler serves every texture.
    // Two settings matter most here:
    //   * filter: LINEAR blends the four nearest texels for a smooth look (vs
    //     NEAREST, which picks one texel and looks blocky when magnified).
    //   * address mode: REPEAT wraps texture coordinates outside [0,1] back into
    //     range, so a UV running 0..N tiles the image N times — that's what lets
    //     the ground plane repeat the checkerboard across the floor.
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter     = SDL_GPU_FILTER_LINEAR;   // minification (texture shrunk)
    info.mag_filter     = SDL_GPU_FILTER_LINEAR;   // magnification (texture enlarged)
    info.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;

    sampler_ = SDL_CreateGPUSampler(device_, &info);
    if (sampler_ == nullptr) {
        KOI_ERROR("Failed to create sampler: %s", SDL_GetError());
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

SDL_GPUTexture* GpuRenderer::uploadToGpuTexture(const void* pixels, Uint32 width,
                                                Uint32 height) {
    // The 2D sibling of uploadToGpuBuffer: same staging dance (CPU-visible transfer
    // buffer → copy pass → GPU resource), but the destination is a texture and the
    // copy is a texture region. (captureFrame does the reverse to read pixels back.)

    // 1. The destination: a 2D texture we can sample in a shader.
    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type                 = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;  // 8-bit RGBA
    texInfo.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;          // read in a shader
    texInfo.width                = width;
    texInfo.height               = height;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels           = 1;  // a single mip level — no mipmaps yet
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &texInfo);
    if (texture == nullptr) {
        KOI_ERROR("uploadToGpuTexture: failed to create texture: %s", SDL_GetError());
        return nullptr;
    }

    // 2. A CPU-visible UPLOAD transfer buffer holding the tightly-packed RGBA pixels.
    const Uint32 byteSize = width * height * 4;  // 4 bytes per texel
    SDL_GPUTransferBufferCreateInfo tbInfo = {};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size  = byteSize;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tbInfo);
    if (transfer == nullptr) {
        KOI_ERROR("uploadToGpuTexture: failed to create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(device_, texture);
        return nullptr;
    }

    // 3. Map, copy the pixels in, unmap.
    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, /*cycle=*/false);
    if (mapped == nullptr) {
        KOI_ERROR("uploadToGpuTexture: failed to map transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUTexture(device_, texture);
        return nullptr;
    }
    std::memcpy(mapped, pixels, byteSize);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    // 4. Record + submit a copy pass that uploads the staging pixels into the
    //    texture. pixels_per_row/rows_per_layer describe the SOURCE layout (tightly
    //    packed); the region describes the DESTINATION rectangle.
    SDL_GPUCommandBuffer* cmd  = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass*      copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {};
    src.transfer_buffer = transfer;
    src.offset          = 0;
    src.pixels_per_row  = width;
    src.rows_per_layer  = height;
    SDL_GPUTextureRegion dst = {};
    dst.texture = texture;
    dst.w       = width;
    dst.h       = height;
    dst.d       = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, /*cycle=*/false);
    SDL_EndGPUCopyPass(copy);

    // One-time init upload: no fence needed (command buffers run in submission
    // order, so the later draw sees the finished copy), and releasing the transfer
    // buffer now is safe (SDL defers the free until the GPU is done).
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return texture;
}

std::shared_ptr<Mesh> GpuRenderer::createMesh(std::span<const Vertex> vertices,
                                              std::span<const Uint16> indices) {
    // Upload both halves of the geometry through the same staging path the cube
    // used in earlier steps. size_bytes() is the element count times sizeof, i.e.
    // exactly the number of bytes to copy into each GPU buffer.
    SDL_GPUBuffer* vbo = uploadToGpuBuffer(SDL_GPU_BUFFERUSAGE_VERTEX,
                                           vertices.data(),
                                           static_cast<Uint32>(vertices.size_bytes()));
    SDL_GPUBuffer* ibo = uploadToGpuBuffer(SDL_GPU_BUFFERUSAGE_INDEX,
                                           indices.data(),
                                           static_cast<Uint32>(indices.size_bytes()));

    if (vbo == nullptr || ibo == nullptr) {
        // Release whichever upload succeeded so a half-built mesh doesn't leak.
        if (vbo != nullptr) SDL_ReleaseGPUBuffer(device_, vbo);
        if (ibo != nullptr) SDL_ReleaseGPUBuffer(device_, ibo);
        KOI_ERROR("createMesh: geometry upload failed.");
        return nullptr;
    }

    // Hand the two buffers to a Mesh, which now owns them and will free them (via
    // the same device) when the last shared_ptr to it drops.
    return std::make_shared<Mesh>(device_, vbo, ibo,
                                  static_cast<Uint32>(indices.size()));
}

std::shared_ptr<Texture> GpuRenderer::loadTexture(const char* path) {
    // 1. Load the image file. SDL_LoadBMP reads the BMP format with no extra
    //    library (so we avoid an SDL_image dependency) and returns a CPU surface.
    SDL_Surface* surface = SDL_LoadBMP(path);
    if (surface == nullptr) {
        KOI_ERROR("loadTexture: SDL_LoadBMP('%s') failed: %s", path, SDL_GetError());
        return nullptr;
    }

    // 2. Convert to a known 32-bit RGBA layout that matches our GPU texture format.
    //    SDL_PIXELFORMAT_RGBA32 is an endian-aware alias whose byte order lines up
    //    with SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, so the bytes need no swizzling
    //    regardless of how the BMP stored its pixels.
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (rgba == nullptr) {
        KOI_ERROR("loadTexture: SDL_ConvertSurface failed: %s", SDL_GetError());
        return nullptr;
    }

    const Uint32 width  = static_cast<Uint32>(rgba->w);
    const Uint32 height = static_cast<Uint32>(rgba->h);

    // 3. Repack into a tightly-packed (pitch == width*4) buffer. A surface's `pitch`
    //    (bytes per row) can include padding; the GPU uploader wants no gaps, so we
    //    copy row by row. (For our 256×256 RGBA image the pitch is already tight, but
    //    this keeps the loader correct for any width.)
    std::vector<Uint8> packed(static_cast<size_t>(width) * height * 4);
    const Uint8* srcRows = static_cast<const Uint8*>(rgba->pixels);
    for (Uint32 row = 0; row < height; ++row) {
        std::memcpy(packed.data() + static_cast<size_t>(row) * width * 4,
                    srcRows + static_cast<size_t>(row) * static_cast<size_t>(rgba->pitch),
                    static_cast<size_t>(width) * 4);
    }
    SDL_DestroySurface(rgba);

    // 4. Upload the pixels into a GPU texture and wrap it in a Texture (which now
    //    owns it and frees it via the same device when its last shared_ptr drops).
    SDL_GPUTexture* gpuTex = uploadToGpuTexture(packed.data(), width, height);
    if (gpuTex == nullptr) {
        return nullptr;  // uploadToGpuTexture already logged the cause
    }

    KOI_INFO("Loaded texture '%s' (%ux%u).", path, width, height);
    return std::make_shared<Texture>(device_, gpuTex, width, height);
}

// Choose a depth texture format the device supports — see chooseDepthFormat above.
bool GpuRenderer::ensureDepthTexture(Uint32 width, Uint32 height) {
    // Reuse the existing texture if it already matches the requested size.
    if (depthTexture_ != nullptr && depthWidth_ == width && depthHeight_ == height) {
        return true;
    }

    // Size changed (or first use): drop the old one and make a fresh depth target.
    if (depthTexture_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, depthTexture_);
        depthTexture_ = nullptr;
    }

    SDL_GPUTextureCreateInfo info = {};
    info.type                 = SDL_GPU_TEXTURETYPE_2D;
    info.format               = depthFormat_;
    info.usage                = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width                = width;
    info.height               = height;
    info.layer_count_or_depth = 1;
    info.num_levels           = 1;
    depthTexture_ = SDL_CreateGPUTexture(device_, &info);
    if (depthTexture_ == nullptr) {
        KOI_ERROR("Failed to create depth texture (%ux%u): %s", width, height, SDL_GetError());
        return false;
    }
    depthWidth_  = width;
    depthHeight_ = height;
    return true;
}

void GpuRenderer::recordScene(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                              const Node& root, const Mat4& view, float aspect,
                              const Vec3& cameraPos) const {
    // Bind the one pipeline every mesh shares. Per-mesh vertex/index buffers AND
    // per-material textures are now bound inside recordNode, just before each node's
    // draw — different nodes use different materials, so binding can't be hoisted
    // out of the loop the way the single global texture was in Step 7.
    SDL_BindGPUGraphicsPipeline(pass, trianglePipeline_);

    // Push the per-frame lighting uniform (fragment set=3, binding 0): one fixed directional
    // "sun" plus the camera position (for specular). It's constant across the whole
    // frame, so we push it ONCE here rather than per node. We use vec4s to match the
    // shader's std140 layout (a vec3 there pads to 16 bytes anyway).
    struct LightUniform {
        float lightDir[4];
        float lightColor[4];
        float ambient[4];
        float cameraPos[4];
    };
    const Vec3 sun = normalize(Vec3{-0.4f, -1.0f, -0.3f});  // direction the light travels
    const LightUniform light = {
        { sun.x, sun.y, sun.z, 0.0f },
        { 1.0f, 1.0f, 1.0f, 0.0f },              // white light
        { 0.15f, 0.15f, 0.18f, 0.0f },           // soft ambient fill
        { cameraPos.x, cameraPos.y, cameraPos.z, 0.0f },
    };
    SDL_PushGPUFragmentUniformData(cmd, /*slot=*/0, &light, sizeof(light));

    // The projection depends only on the viewport, and the view only on the
    // camera, so combine them ONCE here. Each node then only adds one multiply by
    // its own world matrix: mvp = (proj * view) * world.
    const Mat4 projection = perspective(radians(60.0f), aspect, 0.1f, 100.0f);
    const Mat4 projView   = projection * view;

    // Walk the scene tree, drawing as we go. The nodes' world matrices were
    // computed by Node::updateWorldTransforms() before this render pass began.
    recordNode(cmd, pass, root, projView);
}

void GpuRenderer::recordNode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                             const Node& node, const Mat4& projView) const {
    const Mesh*     mesh     = node.mesh();
    const Material* material = node.material();
    // A node is drawn only if it has BOTH a shape (mesh) and an appearance
    // (material). Pure group/pivot nodes have neither and just pass their transform
    // down to their children.
    if (mesh != nullptr && material != nullptr) {
        // Bind THIS material's texture (+ the shared sampler) for the upcoming draw.
        // Unlike Step 7's single global texture, each node can use a different one,
        // so the texture is (re)bound per draw — the same per-node rhythm we already
        // use for the mesh buffers below.
        SDL_GPUTextureSamplerBinding samplerBinding = {};
        samplerBinding.texture = material->texture->handle();
        samplerBinding.sampler = sampler_;
        SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/0, &samplerBinding, /*num_bindings=*/1);

        // Push this material's parameters (fragment set=3, binding 1): a vec4 with
        // shininess in x and specular strength in y. Per draw, so each object can be
        // glossier or duller than the next.
        const float materialParams[4] = { material->shininess, material->specStrength,
                                          0.0f, 0.0f };
        SDL_PushGPUFragmentUniformData(cmd, /*slot=*/1, materialParams, sizeof(materialParams));

        // Bind THIS mesh's geometry. Different meshes (e.g. cube vs. ground plane)
        // live in different buffers, so we (re)bind per drawn node.
        SDL_GPUBufferBinding vertexBinding = {};
        vertexBinding.buffer = mesh->vertexBuffer();
        SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vertexBinding, /*num_bindings=*/1);
        SDL_GPUBufferBinding indexBinding = {};
        indexBinding.buffer = mesh->indexBuffer();
        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

        // Push the two matrices the vertex shader needs: the full MVP (proj·view·
        // world, to clip space) AND the model/world matrix on its own (so the
        // fragment shader can light in world space). `model` is the node's world
        // matrix, already computed by updateWorldTransforms — world already folds in
        // every ancestor's transform, so parented objects move as a group for free.
        struct VertexUniform { Mat4 mvp; Mat4 model; };
        const Mat4 model = node.worldMatrix();
        const VertexUniform u = { projView * model, model };
        SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, &u, sizeof(u));
        SDL_DrawGPUIndexedPrimitives(pass, mesh->indexCount(), /*num_instances=*/1,
                                     /*first_index=*/0, /*vertex_offset=*/0,
                                     /*first_instance=*/0);
    }

    // Recurse into the rest of the subtree.
    for (const std::unique_ptr<Node>& child : node.children()) {
        recordNode(cmd, pass, *child, projView);
    }
}

GpuRenderer::~GpuRenderer() {
    if (device_ != nullptr) {
        // Release the GPU resources we still own (depth texture + pipeline) before
        // tearing down the device. Meshes are NOT freed here: each Mesh owns its
        // own buffers and must already be gone by now — the Engine releases the
        // scene (and thus every mesh) BEFORE destroying this renderer.
        if (depthTexture_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, depthTexture_);
            depthTexture_ = nullptr;
        }
        if (sampler_ != nullptr) {
            SDL_ReleaseGPUSampler(device_, sampler_);
            sampler_ = nullptr;
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

void GpuRenderer::renderFrame(const SDL_FColor& clearColor, const Mat4& view,
                              const Node& sceneRoot, const Vec3& cameraPos) {
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
    //  We capture the acquired image's width/height: the depth buffer must match
    //  its size, and the aspect ratio feeds the projection matrix.
    SDL_GPUTexture* swapchainTexture = nullptr;
    Uint32 width = 0, height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swapchainTexture,
                                               &width, &height)) {
        KOI_ERROR("Failed to acquire swapchain texture: %s", SDL_GetError());
        // Even on failure we must hand the command buffer back, or it leaks.
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    // The call can succeed yet return a null texture when there's nothing to
    // draw into — e.g. the window is minimized. That's not an error: we simply
    // skip the render pass this frame but still submit the (empty) buffer.
    if (swapchainTexture != nullptr && ensureDepthTexture(width, height)) {
        // --------------------------------------------------------------------
        //  3. Describe and begin a render pass.
        //     A render pass declares which texture(s) we're drawing into and
        //     what to do with their existing contents:
        //       load_op  = CLEAR  -> wipe to clear_color before drawing.
        //       store_op = STORE  -> keep the result so it can be displayed.
        //     The clear gives us the dark-teal background; the cube is drawn
        //     on top of it. The depth target is cleared to 1.0 (the far plane)
        //     so every fragment initially passes the LESS test.
        // --------------------------------------------------------------------
        SDL_GPUColorTargetInfo colorTarget = {};
        colorTarget.texture     = swapchainTexture;
        colorTarget.clear_color = clearColor;
        colorTarget.load_op     = SDL_GPU_LOADOP_CLEAR;
        colorTarget.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depthTarget = {};
        depthTarget.texture          = depthTexture_;
        depthTarget.clear_depth      = 1.0f;
        depthTarget.load_op          = SDL_GPU_LOADOP_CLEAR;
        depthTarget.store_op         = SDL_GPU_STOREOP_DONT_CARE;  // not needed after the frame
        // We have no stencil (depth-only format), but these default to LOAD (enum
        // value 0). LOAD is incompatible with cycle=true, so set them explicitly.
        depthTarget.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depthTarget.cycle            = true;  // safe to reuse: we clear it every frame

        SDL_GPURenderPass* pass =
            SDL_BeginGPURenderPass(cmd, &colorTarget, /*num_color_targets=*/1, &depthTarget);

        // Draw the scene through the camera's view matrix. The aspect ratio comes
        // from the actual swapchain size, so the image is never stretched and
        // adapts automatically when the window is resized.
        const float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        recordScene(cmd, pass, sceneRoot, view, aspect, cameraPos);

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

bool GpuRenderer::captureFrame(const char* path, const SDL_FColor& clearColor,
                               const Mat4& view, const Node& sceneRoot,
                               const Vec3& cameraPos) {
    // Fixed capture resolution: keeps the output deterministic and small. The
    // aspect ratio (1280/720) feeds the projection so the cube isn't distorted.
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

    // A matching depth target, so the off-screen cube is depth-tested exactly
    // like the on-screen one.
    if (!ensureDepthTexture(kWidth, kHeight)) {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUTexture(device_, target);
        return false;
    }

    // 3. Record: draw the cube into the off-screen texture, then a copy pass
    //    that downloads that texture into the transfer buffer.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);

    SDL_GPUColorTargetInfo colorTarget = {};
    colorTarget.texture     = target;
    colorTarget.clear_color = clearColor;
    colorTarget.load_op     = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op    = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depthTarget = {};
    depthTarget.texture          = depthTexture_;
    depthTarget.clear_depth      = 1.0f;
    depthTarget.load_op          = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op         = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.cycle            = true;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthTarget);
    // Draw the same scene the live path does, through the supplied `view` (the
    // camera's default pose) — deterministic because nothing here is time-based.
    constexpr float kCaptureAspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    recordScene(cmd, pass, sceneRoot, view, kCaptureAspect, cameraPos);
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
