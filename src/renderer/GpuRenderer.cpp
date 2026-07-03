#include "renderer/GpuRenderer.hpp"

#include <array>    // std::array (the six cubemap face paths / faces)
#include <cstddef>  // offsetof
#include <cstring>  // std::memcpy
#include <memory>   // std::make_shared
#include <vector>   // std::vector (repack texture rows to a tight pitch)

#include "core/Log.hpp"
#include "math/Mat4.hpp"
#include "renderer/Mesh.hpp"
#include "renderer/Primitives.hpp"  // makeCubeMesh — reused as the skybox cube
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

namespace {
// Post-processing (Step 10) constants.
// The off-screen scene is rendered in HDR — 16-bit float per channel — so highlights
// can exceed 1.0. That extra range is what gives tone-mapping real work to do and what
// makes bloom's "bright-pass" meaningful (an 8-bit target would clamp it all away).
constexpr SDL_GPUTextureFormat kSceneHdrFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
// How many horizontal+vertical blur iterations the bloom glow gets (more = wider/softer).
constexpr int kBloomBlurPasses = 4;
// Vignette darkening strength when enabled (screen corners end up ~1 - strength bright).
constexpr float kVignetteStrength = 0.5f;
// Clear colour for intermediate post targets (only used when a pass needs a defined fill).
constexpr SDL_FColor kPostClearBlack = {0.0f, 0.0f, 0.0f, 1.0f};
}  // namespace

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

    // ------------------------------------------------------------------------
    //  4b. Create the neutral 1×1 fallback maps (Step 13) a material binds when it
    //      lacks a metallic-roughness / normal / AO map. If this fails, isValid()
    //      stays false and the Engine reports it.
    // ------------------------------------------------------------------------
    if (!createFallbackTextures()) {
        return;
    }

    // ------------------------------------------------------------------------
    //  5. Create the shadow-mapping resources (shadow map texture, its sampler,
    //     and the depth-only pipeline). If this fails, isValid() reports it.
    // ------------------------------------------------------------------------
    if (!createShadowResources()) {
        return;
    }

    // ------------------------------------------------------------------------
    //  6. Create the post-processing resources (Step 10): the shared sampler and
    //     the four fullscreen pipelines (bloom bright-pass, blur, tone-map
    //     composite, FXAA). If this fails, isValid() reports it.
    // ------------------------------------------------------------------------
    if (!createPostResources()) {
        return;
    }

    // ------------------------------------------------------------------------
    //  7. Create the skybox resources (Step 14): the skybox pipeline, its
    //     CLAMP_TO_EDGE cubemap sampler, and the cube mesh drawn around the camera.
    //     The cubemap TEXTURE is loaded later by the Engine (loadCubemap). If this
    //     fails, isValid() reports it.
    // ------------------------------------------------------------------------
    if (!createSkyboxResources()) {
        return;
    }

    // Note: no geometry or textures are uploaded here. The renderer is a factory
    // for meshes (createMesh) and textures (loadTexture) — the Engine builds the
    // scene (geometry + materials) after this constructor returns.
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
    // Step 13: the fragment shader now reads FIVE samplers — the four per-material maps
    // (albedo, metallic-roughness, normal, AO) plus the shared shadow map.
    SDL_GPUShader* fragmentShader = loadShader(device_, "triangle.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, /*numUniformBuffers=*/2, /*numSamplers=*/5);
    if (vertexShader == nullptr || fragmentShader == nullptr) {
        // loadShader already logged the cause. Clean up whichever did load.
        if (vertexShader != nullptr)   SDL_ReleaseGPUShader(device_, vertexShader);
        if (fragmentShader != nullptr) SDL_ReleaseGPUShader(device_, fragmentShader);
        return false;
    }

    // A graphics pipeline bakes the shaders together with fixed-function state.
    // We must tell it the pixel format of what we render into. Since Step 10 the
    // scene no longer renders straight to the swapchain — it renders into our own
    // off-screen HDR target (kSceneHdrFormat) that the post-processing chain then
    // tone-maps down to the swapchain. So the pipeline's color format is the HDR one.
    if (!SDL_GPUTextureSupportsFormat(device_, kSceneHdrFormat, SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                      SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        KOI_ERROR("HDR scene format (R16G16B16A16_FLOAT) is not a supported sampleable "
                  "color target on this device.");
        SDL_ReleaseGPUShader(device_, vertexShader);
        SDL_ReleaseGPUShader(device_, fragmentShader);
        return false;
    }
    SDL_GPUColorTargetDescription colorTargetDesc = {};
    colorTargetDesc.format = kSceneHdrFormat;

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

    const SDL_GPUVertexAttribute vertexAttributes[5] = {
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
        // location 4: inTangent (vec3) right after the normal — Step 13's TBN basis.
        { /*location=*/4, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          offsetof(Vertex, tangent) },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.vertex_shader   = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
    pipelineInfo.vertex_input_state.num_vertex_buffers         = 1;
    pipelineInfo.vertex_input_state.vertex_attributes          = vertexAttributes;
    pipelineInfo.vertex_input_state.num_vertex_attributes      = 5;
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
    // Step 13 makes two of these settings finally bite. `mipmap_mode = LINEAR` now
    // interpolates BETWEEN mip levels (trilinear filtering) — meaningful only now that
    // textures actually carry a mip chain. And ANISOTROPIC filtering fixes the blur on
    // surfaces viewed at a grazing angle (like the receding floor): a plain mipmapped
    // sampler must pick one square level for the whole pixel and over-blurs along the
    // stretched axis, whereas anisotropy takes several taps along that axis to keep it
    // sharp. max_anisotropy caps how many taps (8 is a common quality/cost balance).
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter        = SDL_GPU_FILTER_LINEAR;   // minification (texture shrunk)
    info.mag_filter        = SDL_GPU_FILTER_LINEAR;   // magnification (texture enlarged)
    info.mipmap_mode       = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w    = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.enable_anisotropy = true;
    info.max_anisotropy    = 8.0f;

    sampler_ = SDL_CreateGPUSampler(device_, &info);
    if (sampler_ == nullptr) {
        KOI_ERROR("Failed to create sampler: %s", SDL_GetError());
        return false;
    }
    return true;
}

SDL_GPUTexture* GpuRenderer::createSolidTexture(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    // A 1×1 RGBA texel, uploaded through the usual path (no mips — it's a single pixel).
    const Uint8 texel[4] = {r, g, b, a};
    return uploadToGpuTexture(texel, /*width=*/1, /*height=*/1, /*withMips=*/false);
}

bool GpuRenderer::createFallbackTextures() {
    // whiteTex_ (255,255,255,255): a material without a metallic-roughness or AO map
    // binds this, so `factor × sampledChannel` becomes just the scalar factor and
    // `ao` becomes 1 — identical to the Step 12 scalar-only behaviour.
    whiteTex_ = createSolidTexture(255, 255, 255, 255);
    // flatNormalTex_ (128,128,255,255): decodes (×2−1) to tangent-space (0,0,1), a
    // normal pointing straight out of the surface — i.e. no perturbation. So a
    // material without a normal map is lit by its geometric normal, unchanged.
    flatNormalTex_ = createSolidTexture(128, 128, 255, 255);
    if (whiteTex_ == nullptr || flatNormalTex_ == nullptr) {
        KOI_ERROR("Failed to create fallback textures.");
        return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
//  Shadow mapping helpers (file-local).
// ----------------------------------------------------------------------------
namespace {

// The shadow map's resolution. Bigger = crisper shadows, more memory/fill. A
// directional light covers the whole scene with one map, so 2048² is plenty here.
constexpr Uint32 kShadowSize = 2048;

// A safe fallback sun direction, used only if the scene somehow ships no lights.
// Normally the shadow-casting sun's direction comes from lights[0] (Step 11).
constexpr Vec3 kFallbackSunDir = {-0.4f, -1.0f, -0.3f};

// Build the light's view-projection: an orthographic box (parallel sun rays) looking
// along the sun direction at the scene center. This one matrix both renders the
// shadow map (pass 1) and decodes it in the fragment shader (pass 2), so they must
// agree exactly. `sunDir` is the direction the sun's rays TRAVEL (from lights[0]),
// so the shadow always follows the actual directional light. The box (±kHalf) and
// range are sized to enclose the whole scene.
Mat4 computeLightViewProj(const Vec3& sunDir) {
    const Vec3 dir    = normalize(sunDir);
    const Vec3 center = {0.0f, -0.5f, 0.0f};   // roughly the middle of the scene
    const float back  = 18.0f;                 // how far back along the sun to place the eye
    const Vec3 eye    = center - dir * back;
    const Mat4 view   = lookAt(eye, center, Vec3{0.0f, 1.0f, 0.0f});
    const float kHalf = 11.0f;                 // half-width of the covered area
    const Mat4 proj   = orthographic(-kHalf, kHalf, -kHalf, kHalf, 0.1f, back * 2.0f);
    return proj * view;
}

}  // namespace

bool GpuRenderer::createShadowResources() {
    // 1. The shadow map: a depth texture we both RENDER INTO (pass 1) and SAMPLE
    //    (pass 2). It needs both usages, and the same depth format the main pipeline
    //    chose so the depth values are comparable.
    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type                 = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format               = depthFormat_;
    texInfo.usage                = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                   SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texInfo.width                = kShadowSize;
    texInfo.height               = kShadowSize;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels           = 1;
    shadowMap_ = SDL_CreateGPUTexture(device_, &texInfo);
    if (shadowMap_ == nullptr) {
        KOI_ERROR("Failed to create shadow map texture: %s", SDL_GetError());
        return false;
    }

    // 2. The shadow sampler: CLAMP_TO_EDGE so a fragment whose light-space position
    //    falls outside the map reads the (far) edge and counts as lit, never tiled.
    SDL_GPUSamplerCreateInfo sInfo = {};
    sInfo.min_filter     = SDL_GPU_FILTER_LINEAR;
    sInfo.mag_filter     = SDL_GPU_FILTER_LINEAR;
    sInfo.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shadowSampler_ = SDL_CreateGPUSampler(device_, &sInfo);
    if (shadowSampler_ == nullptr) {
        KOI_ERROR("Failed to create shadow sampler: %s", SDL_GetError());
        return false;
    }

    // 3. The depth-only pipeline: shadow.vert positions vertices in the light's clip
    //    space; shadow.frag is empty. NO color target — we only want the depth the
    //    rasterizer writes. The vertex input reads just position, but at the full
    //    Vertex stride (the meshes carry color/uv/normal too).
    SDL_GPUShader* vs = loadShader(device_, "shadow.vert", SDL_GPU_SHADERSTAGE_VERTEX,
                                   /*numUniformBuffers=*/1);
    SDL_GPUShader* fs = loadShader(device_, "shadow.frag", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vs == nullptr || fs == nullptr) {
        if (vs != nullptr) SDL_ReleaseGPUShader(device_, vs);
        if (fs != nullptr) SDL_ReleaseGPUShader(device_, fs);
        return false;
    }

    SDL_GPUVertexBufferDescription vbDesc = {};
    vbDesc.slot       = 0;
    vbDesc.pitch      = static_cast<Uint32>(sizeof(Vertex));
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    const SDL_GPUVertexAttribute posAttr = {
        /*location=*/0, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        offsetof(Vertex, position)};

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader   = vs;
    info.fragment_shader = fs;
    info.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
    info.vertex_input_state.num_vertex_buffers         = 1;
    info.vertex_input_state.vertex_attributes          = &posAttr;
    info.vertex_input_state.num_vertex_attributes      = 1;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.depth_stencil_state.enable_depth_test  = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS;
    // A small depth bias during the shadow pass nudges stored depths away from the
    // light, reducing "shadow acne" (a surface shadowing itself) at the source.
    info.rasterizer_state.enable_depth_bias         = true;
    info.rasterizer_state.depth_bias_constant_factor = 1.25f;
    info.rasterizer_state.depth_bias_slope_factor    = 1.75f;
    info.target_info.num_color_targets        = 0;       // depth only — no color
    info.target_info.has_depth_stencil_target = true;
    info.target_info.depth_stencil_format     = depthFormat_;

    shadowPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);
    SDL_ReleaseGPUShader(device_, vs);
    SDL_ReleaseGPUShader(device_, fs);
    if (shadowPipeline_ == nullptr) {
        KOI_ERROR("Failed to create shadow pipeline: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool GpuRenderer::createSkyboxResources() {
    // 1. The skybox pipeline. skybox.vert reads one vertex uniform (the
    //    translation-stripped view-projection); skybox.frag reads one cubemap sampler.
    SDL_GPUShader* vs = loadShader(device_, "skybox.vert", SDL_GPU_SHADERSTAGE_VERTEX,
                                   /*numUniformBuffers=*/1);
    SDL_GPUShader* fs = loadShader(device_, "skybox.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
                                   /*numUniformBuffers=*/0, /*numSamplers=*/1);
    if (vs == nullptr || fs == nullptr) {
        if (vs != nullptr) SDL_ReleaseGPUShader(device_, vs);
        if (fs != nullptr) SDL_ReleaseGPUShader(device_, fs);
        return false;
    }

    // The cube mesh carries the full Vertex layout, but the sky only needs the
    // position (its object-space corner IS the sample direction) — so, exactly like
    // the shadow pass, we declare a single position attribute at the full stride.
    SDL_GPUVertexBufferDescription vbDesc = {};
    vbDesc.slot       = 0;
    vbDesc.pitch      = static_cast<Uint32>(sizeof(Vertex));
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    const SDL_GPUVertexAttribute posAttr = {
        /*location=*/0, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        offsetof(Vertex, position)};

    // Same HDR color target + depth format as the main pass — the sky is drawn INTO
    // the scene pass, not as a separate fullscreen pass.
    SDL_GPUColorTargetDescription colorTargetDesc = {};
    colorTargetDesc.format = kSceneHdrFormat;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader   = vs;
    info.fragment_shader = fs;
    info.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
    info.vertex_input_state.num_vertex_buffers         = 1;
    info.vertex_input_state.vertex_attributes          = &posAttr;
    info.vertex_input_state.num_vertex_attributes      = 1;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    // The depth trick: the sky emits depth 1.0 (far plane) for every fragment. We
    // keep the depth TEST but compare with LEQUAL — so a sky fragment survives where
    // the depth buffer is still at its 1.0 clear (background) and is rejected where
    // geometry already wrote a nearer depth. Depth WRITE is OFF: the sky must not
    // occlude anything or leave a wall of far-plane depth behind.
    info.depth_stencil_state.enable_depth_test  = true;
    info.depth_stencil_state.enable_depth_write = false;
    info.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.target_info.num_color_targets         = 1;
    info.target_info.color_target_descriptions = &colorTargetDesc;
    info.target_info.has_depth_stencil_target  = true;
    info.target_info.depth_stencil_format      = depthFormat_;
    // Culling stays off (engine default): we're inside the cube, and with depth-write
    // off both facings resolve to the same far-plane sky colour anyway.

    skyboxPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);
    SDL_ReleaseGPUShader(device_, vs);
    SDL_ReleaseGPUShader(device_, fs);
    if (skyboxPipeline_ == nullptr) {
        KOI_ERROR("Failed to create skybox pipeline: %s", SDL_GetError());
        return false;
    }

    // 2. The cubemap sampler. CLAMP_TO_EDGE on ALL THREE axes is essential: a cube
    //    face's edge texels must clamp (not wrap) so the six faces meet seamlessly.
    //    Linear + trilinear (mipmap) filtering keeps the sky smooth as it minifies.
    SDL_GPUSamplerCreateInfo sInfo = {};
    sInfo.min_filter     = SDL_GPU_FILTER_LINEAR;
    sInfo.mag_filter     = SDL_GPU_FILTER_LINEAR;
    sInfo.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    skyboxSampler_ = SDL_CreateGPUSampler(device_, &sInfo);
    if (skyboxSampler_ == nullptr) {
        KOI_ERROR("Failed to create skybox sampler: %s", SDL_GetError());
        return false;
    }

    // 3. The cube mesh drawn around the camera. We reuse makeCubeMesh — the sky only
    //    reads its positions, so the extra per-vertex data is simply ignored.
    skyboxMesh_ = makeCubeMesh(*this);
    if (skyboxMesh_ == nullptr) {
        KOI_ERROR("Failed to create skybox cube mesh.");
        return false;
    }
    return true;
}

void GpuRenderer::renderShadowPass(SDL_GPUCommandBuffer* cmd, const Node& root,
                                   const Mat4& lightViewProj) const {
    // PASS 1: render the scene's depth into the shadow map, from the light's view.
    // A render pass with a depth target but NO color target. STORE the result — the
    // main pass samples it.
    SDL_GPUDepthStencilTargetInfo depthTarget = {};
    depthTarget.texture          = shadowMap_;
    depthTarget.clear_depth      = 1.0f;
    depthTarget.load_op          = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op         = SDL_GPU_STOREOP_STORE;   // sampled next pass → keep it
    depthTarget.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.cycle            = true;

    SDL_GPURenderPass* pass =
        SDL_BeginGPURenderPass(cmd, /*color_targets=*/nullptr, /*num_color=*/0, &depthTarget);
    SDL_BindGPUGraphicsPipeline(pass, shadowPipeline_);
    recordShadowNode(cmd, pass, root, lightViewProj);
    SDL_EndGPURenderPass(pass);
}

void GpuRenderer::recordShadowNode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                                   const Node& node, const Mat4& lightViewProj) const {
    // Draw whatever has geometry (material/texture irrelevant — we only write depth).
    if (const Mesh* mesh = node.mesh()) {
        SDL_GPUBufferBinding vertexBinding = {};
        vertexBinding.buffer = mesh->vertexBuffer();
        SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vertexBinding, /*num_bindings=*/1);
        SDL_GPUBufferBinding indexBinding = {};
        indexBinding.buffer = mesh->indexBuffer();
        SDL_BindGPUIndexBuffer(pass, &indexBinding, mesh->indexElementSize());

        const Mat4 lightMvp = lightViewProj * node.worldMatrix();
        SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, &lightMvp, sizeof(lightMvp));
        SDL_DrawGPUIndexedPrimitives(pass, mesh->indexCount(), /*num_instances=*/1,
                                     /*first_index=*/0, /*vertex_offset=*/0,
                                     /*first_instance=*/0);
    }
    for (const std::unique_ptr<Node>& child : node.children()) {
        recordShadowNode(cmd, pass, *child, lightViewProj);
    }
}

// ----------------------------------------------------------------------------
//  Post-processing (Step 10).
// ----------------------------------------------------------------------------

SDL_GPUTexture* GpuRenderer::createColorTarget(Uint32 width, Uint32 height,
                                               SDL_GPUTextureFormat format) {
    // A texture we can BOTH render into and later SAMPLE — the defining trait of an
    // off-screen post-processing target.
    SDL_GPUTextureCreateInfo info = {};
    info.type                 = SDL_GPU_TEXTURETYPE_2D;
    info.format               = format;
    info.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width                = width;
    info.height               = height;
    info.layer_count_or_depth = 1;
    info.num_levels           = 1;
    return SDL_CreateGPUTexture(device_, &info);
}

SDL_GPUGraphicsPipeline* GpuRenderer::buildFullscreenPipeline(SDL_GPUShader* vs,
                                                              const char* frag,
                                                              SDL_GPUTextureFormat targetFormat,
                                                              Uint32 numSamplers) {
    // Each post pass shares fullscreen.vert and adds its own fragment shader. Every
    // post fragment shader declares exactly one uniform buffer (its parameters).
    SDL_GPUShader* fs = loadShader(device_, frag, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                   /*numUniformBuffers=*/1, numSamplers);
    if (fs == nullptr) {
        return nullptr;  // loadShader already logged the cause
    }

    SDL_GPUColorTargetDescription colorDesc = {};
    colorDesc.format = targetFormat;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader   = vs;
    info.fragment_shader = fs;
    // No vertex input at all: the fullscreen triangle is generated from gl_VertexIndex,
    // so there is no vertex buffer, no attributes, and we draw 3 vertices non-indexed.
    info.vertex_input_state.num_vertex_buffers    = 0;
    info.vertex_input_state.num_vertex_attributes = 0;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    // No depth: a post pass just paints over every pixel; visibility was already
    // resolved when the scene was drawn into the HDR target.
    info.target_info.num_color_targets         = 1;
    info.target_info.color_target_descriptions = &colorDesc;
    info.target_info.has_depth_stencil_target  = false;

    SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(device_, &info);
    SDL_ReleaseGPUShader(device_, fs);  // the pipeline keeps what it needs
    if (pipe == nullptr) {
        KOI_ERROR("Failed to create fullscreen pipeline (%s): %s", frag, SDL_GetError());
    }
    return pipe;
}

bool GpuRenderer::createPostResources() {
    // 1. The sampler all post passes read through: LINEAR (so half-res bloom upsamples
    //    smoothly) and CLAMP_TO_EDGE (a fullscreen pass must never wrap a tap off one
    //    edge to the opposite side). No mipmaps — these targets have a single level.
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_LINEAR;
    si.mag_filter     = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    postSampler_ = SDL_CreateGPUSampler(device_, &si);
    if (postSampler_ == nullptr) {
        KOI_ERROR("Failed to create post sampler: %s", SDL_GetError());
        return false;
    }

    // 2. Load the shared fullscreen vertex shader once, build all four pipelines from
    //    it, then release it (each pipeline copies what it needs).
    SDL_GPUShader* vs = loadShader(device_, "fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX);
    if (vs == nullptr) {
        return false;
    }

    // Bloom passes write HDR (so a bright glow stays bright through the blur); the
    // composite and FXAA passes write the swapchain's LDR format (the final image).
    const SDL_GPUTextureFormat swap = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    brightPipeline_    = buildFullscreenPipeline(vs, "bright.frag",    kSceneHdrFormat, /*numSamplers=*/1);
    blurPipeline_      = buildFullscreenPipeline(vs, "blur.frag",      kSceneHdrFormat, /*numSamplers=*/1);
    compositePipeline_ = buildFullscreenPipeline(vs, "composite.frag", swap,            /*numSamplers=*/2);
    fxaaPipeline_      = buildFullscreenPipeline(vs, "fxaa.frag",      swap,            /*numSamplers=*/1);
    SDL_ReleaseGPUShader(device_, vs);

    if (brightPipeline_ == nullptr || blurPipeline_ == nullptr ||
        compositePipeline_ == nullptr || fxaaPipeline_ == nullptr) {
        return false;  // buildFullscreenPipeline already logged which one failed
    }
    return true;
}

bool GpuRenderer::ensureSceneTargets(Uint32 width, Uint32 height) {
    // The depth buffer tracks the same size (it's attached to the scene pass).
    if (!ensureDepthTexture(width, height)) {
        return false;
    }
    // Reuse existing targets if the size hasn't changed.
    if (sceneHdr_ != nullptr && postWidth_ == width && postHeight_ == height) {
        return true;
    }

    // Size changed (or first use): drop the old targets and rebuild the whole set.
    SDL_ReleaseGPUTexture(device_, sceneHdr_);  // SDL tolerates a null argument
    SDL_ReleaseGPUTexture(device_, ldr_);
    SDL_ReleaseGPUTexture(device_, bloomA_);
    SDL_ReleaseGPUTexture(device_, bloomB_);
    sceneHdr_ = ldr_ = bloomA_ = bloomB_ = nullptr;

    // Full-res scene (HDR) + final LDR (swapchain format, ready for FXAA to copy out).
    const SDL_GPUTextureFormat swap = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    sceneHdr_ = createColorTarget(width, height, kSceneHdrFormat);
    ldr_      = createColorTarget(width, height, swap);

    // Bloom runs at HALF resolution: a blur hides the lost detail and the cost drops ~4x.
    bloomWidth_  = halfExtent(width);
    bloomHeight_ = halfExtent(height);
    bloomA_ = createColorTarget(bloomWidth_, bloomHeight_, kSceneHdrFormat);
    bloomB_ = createColorTarget(bloomWidth_, bloomHeight_, kSceneHdrFormat);

    if (sceneHdr_ == nullptr || ldr_ == nullptr || bloomA_ == nullptr || bloomB_ == nullptr) {
        KOI_ERROR("Failed to create one or more post targets (%ux%u): %s", width, height,
                  SDL_GetError());
        return false;
    }
    postWidth_  = width;
    postHeight_ = height;
    return true;
}

void GpuRenderer::drawFullscreen(SDL_GPURenderPass* pass) {
    // Draw the fullscreen triangle: 3 vertices, NO buffers bound — the vertex shader
    // synthesizes the corners from gl_VertexIndex.
    SDL_DrawGPUPrimitives(pass, /*num_vertices=*/3, /*num_instances=*/1,
                          /*first_vertex=*/0, /*first_instance=*/0);
}

SDL_GPURenderPass* GpuRenderer::beginColorPass(SDL_GPUCommandBuffer* cmd,
                                               SDL_GPUTexture* target,
                                               SDL_GPULoadOp loadOp, bool cycle) const {
    SDL_GPUColorTargetInfo ci = {};
    ci.texture     = target;
    ci.clear_color = kPostClearBlack;
    ci.load_op     = loadOp;
    ci.store_op    = SDL_GPU_STOREOP_STORE;
    // `cycle` rotates to a fresh internal allocation so this frame's write doesn't
    // stall on the previous frame still sampling the same target (same trick the
    // shadow map uses). We cycle our own intermediate targets, but NOT the swapchain
    // image (SDL manages that one).
    ci.cycle = cycle;
    return SDL_BeginGPURenderPass(cmd, &ci, /*num_color_targets=*/1, /*depth=*/nullptr);
}

void GpuRenderer::bindPostSampler(SDL_GPURenderPass* pass, Uint32 slot,
                                  SDL_GPUTexture* tex) const {
    SDL_GPUTextureSamplerBinding b = {};
    b.texture = tex;
    b.sampler = postSampler_;
    SDL_BindGPUFragmentSamplers(pass, slot, &b, /*num_bindings=*/1);
}

void GpuRenderer::runPostChain(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* finalColor,
                               Uint32 width, Uint32 height, const PostSettings& post) const {
    // ---- Bloom: extract bright areas, then blur them into a soft glow -----------
    if (post.bloom) {
        // Bright-pass: sceneHdr_ -> bloomA_ (half-res). The pass covers every texel,
        // so DON'T_CARE the old contents.
        {
            SDL_GPURenderPass* p = beginColorPass(cmd, bloomA_, SDL_GPU_LOADOP_DONT_CARE, true);
            SDL_BindGPUGraphicsPipeline(p, brightPipeline_);
            bindPostSampler(p, /*slot=*/0, sceneHdr_);
            const float bright[4] = {post.bloomThreshold, 0.0f, 0.0f, 0.0f};
            SDL_PushGPUFragmentUniformData(cmd, /*slot=*/0, bright, sizeof(bright));
            drawFullscreen(p);
            SDL_EndGPURenderPass(p);
        }
        // Separable Gaussian, ping-ponging A<->B: horizontal then vertical, repeated.
        const float tx = 1.0f / static_cast<float>(bloomWidth_);
        const float ty = 1.0f / static_cast<float>(bloomHeight_);
        for (int i = 0; i < kBloomBlurPasses; ++i) {
            {   // horizontal: bloomA_ -> bloomB_
                SDL_GPURenderPass* p = beginColorPass(cmd, bloomB_, SDL_GPU_LOADOP_DONT_CARE, true);
                SDL_BindGPUGraphicsPipeline(p, blurPipeline_);
                bindPostSampler(p, 0, bloomA_);
                const float dir[4] = {tx, 0.0f, 0.0f, 0.0f};
                SDL_PushGPUFragmentUniformData(cmd, 0, dir, sizeof(dir));
                drawFullscreen(p);
                SDL_EndGPURenderPass(p);
            }
            {   // vertical: bloomB_ -> bloomA_
                SDL_GPURenderPass* p = beginColorPass(cmd, bloomA_, SDL_GPU_LOADOP_DONT_CARE, true);
                SDL_BindGPUGraphicsPipeline(p, blurPipeline_);
                bindPostSampler(p, 0, bloomB_);
                const float dir[4] = {0.0f, ty, 0.0f, 0.0f};
                SDL_PushGPUFragmentUniformData(cmd, 0, dir, sizeof(dir));
                drawFullscreen(p);
                SDL_EndGPURenderPass(p);
            }
        }
    } else {
        // Bloom disabled: clear bloomA_ to black so the composite reads a defined zero
        // glow (its bloom term is also scaled by 0 below, but this keeps the sampled
        // texture well-defined and avoids relying on stale contents).
        SDL_GPURenderPass* p = beginColorPass(cmd, bloomA_, SDL_GPU_LOADOP_CLEAR, true);
        SDL_EndGPURenderPass(p);
    }

    // ---- Composite: scene + bloom, exposure, tone-map, vignette, gamma -> ldr_ ----
    {
        SDL_GPURenderPass* p = beginColorPass(cmd, ldr_, SDL_GPU_LOADOP_DONT_CARE, true);
        SDL_BindGPUGraphicsPipeline(p, compositePipeline_);
        // Two inputs: the HDR scene (slot 0) and the blurred bloom (slot 1).
        SDL_GPUTextureSamplerBinding binds[2] = {};
        binds[0].texture = sceneHdr_;  binds[0].sampler = postSampler_;
        binds[1].texture = bloomA_;    binds[1].sampler = postSampler_;
        SDL_BindGPUFragmentSamplers(p, /*first_slot=*/0, binds, /*num_bindings=*/2);
        const float params[4] = {
            post.exposure,
            post.bloom ? post.bloomIntensity : 0.0f,
            post.vignette ? kVignetteStrength : 0.0f,
            post.tonemap ? 1.0f : 0.0f,
        };
        SDL_PushGPUFragmentUniformData(cmd, 0, params, sizeof(params));
        drawFullscreen(p);
        SDL_EndGPURenderPass(p);
    }

    // ---- FXAA: anti-alias ldr_ into the final target (swapchain / capture) -------
    // The final target is NOT one of ours, so don't cycle it.
    {
        SDL_GPURenderPass* p = beginColorPass(cmd, finalColor, SDL_GPU_LOADOP_DONT_CARE, false);
        SDL_BindGPUGraphicsPipeline(p, fxaaPipeline_);
        bindPostSampler(p, 0, ldr_);
        const float params[4] = {
            1.0f / static_cast<float>(width),
            1.0f / static_cast<float>(height),
            post.fxaa ? 1.0f : 0.0f,
            0.0f,
        };
        SDL_PushGPUFragmentUniformData(cmd, 0, params, sizeof(params));
        drawFullscreen(p);
        SDL_EndGPURenderPass(p);
    }
}

void GpuRenderer::renderSceneAndPost(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* finalColor,
                                     Uint32 width, Uint32 height, const Mat4& view,
                                     const Node& sceneRoot, const Vec3& cameraPos,
                                     std::span<const Light> lights,
                                     const SDL_FColor& clearColor, const PostSettings& post) {
    if (!ensureSceneTargets(width, height)) {
        return;  // ensureSceneTargets logged; skip this frame rather than crash
    }

    // The shadow map follows the SUN — the directional light at index 0 (by
    // convention). Its direction drives both passes so the shadows line up with the
    // shading, even as the sun is reconfigured. (Only this one light casts a shadow.)
    const Vec3 sunDir = (!lights.empty() && lights[0].type == LightType::Directional)
                            ? lights[0].direction
                            : kFallbackSunDir;

    // PASS 1 — shadow depth from the sun's view.
    const Mat4 lightViewProj = computeLightViewProj(sunDir);
    renderShadowPass(cmd, sceneRoot, lightViewProj);

    // PASS 2 — the scene, into the off-screen HDR target (NOT the swapchain).
    SDL_GPUColorTargetInfo colorTarget = {};
    colorTarget.texture     = sceneHdr_;
    colorTarget.clear_color = clearColor;
    colorTarget.load_op     = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op    = SDL_GPU_STOREOP_STORE;   // the post chain samples it next
    colorTarget.cycle       = true;

    SDL_GPUDepthStencilTargetInfo depthTarget = {};
    depthTarget.texture          = depthTexture_;
    depthTarget.clear_depth      = 1.0f;
    depthTarget.load_op          = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op         = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op  = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.cycle            = true;

    SDL_GPURenderPass* pass =
        SDL_BeginGPURenderPass(cmd, &colorTarget, /*num_color_targets=*/1, &depthTarget);
    const float aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    recordScene(cmd, pass, sceneRoot, view, aspect, cameraPos, lights, lightViewProj);
    SDL_EndGPURenderPass(pass);

    // PASSES 3+ — the fullscreen post-processing chain, ending in `finalColor`.
    runPostChain(cmd, finalColor, width, height, post);
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
                                                Uint32 height, bool withMips) {
    // The 2D sibling of uploadToGpuBuffer: same staging dance (CPU-visible transfer
    // buffer → copy pass → GPU resource), but the destination is a texture and the
    // copy is a texture region. (captureFrame does the reverse to read pixels back.)

    // A MIP CHAIN is the texture pre-shrunk to 1/2, 1/4, 1/8 … down to 1×1. When a
    // textured surface is far away, one screen pixel covers many texels; sampling just
    // the full-res image then aliases (shimmers) as the surface moves. The sampler
    // instead reads a smaller, pre-averaged level — so we compute how many levels fit
    // (floor(log2(largest side)) + 1) and let the GPU generate them below.
    Uint32 mipLevels = 1;
    if (withMips) {
        Uint32 maxDim = (width > height) ? width : height;
        while (maxDim > 1) { maxDim >>= 1; ++mipLevels; }
    }

    // 1. The destination: a 2D texture we can sample in a shader. Generating mipmaps
    //    needs the texture to ALSO be usable as a color target (the GPU renders the
    //    downsampled levels into it), so we add COLOR_TARGET usage when mipping.
    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type                 = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;  // 8-bit RGBA
    texInfo.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                                   (withMips ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0u);
    texInfo.width                = width;
    texInfo.height               = height;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels           = mipLevels;
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

    // Fill the smaller mip levels from the level-0 pixels we just uploaded. This is a
    // GPU-side downsample, so it must run OUTSIDE any copy/render pass — here on the
    // same command buffer, which orders it after the copy above.
    if (withMips) {
        SDL_GenerateMipmapsForGPUTexture(cmd, texture);
    }

    // One-time init upload: no fence needed (command buffers run in submission
    // order, so the later draw sees the finished copy), and releasing the transfer
    // buffer now is safe (SDL defers the free until the GPU is done).
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return texture;
}

SDL_GPUTexture* GpuRenderer::uploadCubemap(const std::array<const void*, 6>& faces,
                                           Uint32 width, Uint32 height) {
    // A cube texture is really SIX 2D layers (one per face). The upload is the same
    // staging dance as uploadToGpuTexture, done once per layer into a texture created
    // with type CUBE and layer_count_or_depth = 6. We keep the mip chain (COLOR_TARGET
    // usage lets the GPU render the downsampled levels) so distant/minified sky stays
    // smooth — and so IBL later has the levels to work from.
    Uint32 mipLevels = 1;
    { Uint32 maxDim = (width > height) ? width : height;
      while (maxDim > 1) { maxDim >>= 1; ++mipLevels; } }

    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type                 = SDL_GPU_TEXTURETYPE_CUBE;
    texInfo.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texInfo.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                                   SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;  // for mip generation
    texInfo.width                = width;
    texInfo.height               = height;
    texInfo.layer_count_or_depth = 6;  // the six cube faces
    texInfo.num_levels           = mipLevels;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &texInfo);
    if (texture == nullptr) {
        KOI_ERROR("uploadCubemap: failed to create cube texture: %s", SDL_GetError());
        return nullptr;
    }

    // One transfer buffer holds all six faces back to back (face i at i*faceBytes).
    const Uint32 faceBytes = width * height * 4;
    SDL_GPUTransferBufferCreateInfo tbInfo = {};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size  = faceBytes * 6;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &tbInfo);
    if (transfer == nullptr) {
        KOI_ERROR("uploadCubemap: failed to create transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(device_, texture);
        return nullptr;
    }
    Uint8* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device_, transfer, false));
    if (mapped == nullptr) {
        KOI_ERROR("uploadCubemap: failed to map transfer buffer: %s", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        SDL_ReleaseGPUTexture(device_, texture);
        return nullptr;
    }
    for (Uint32 i = 0; i < 6; ++i) {
        std::memcpy(mapped + static_cast<size_t>(i) * faceBytes, faces[i], faceBytes);
    }
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    // Copy each face from its slice of the staging buffer into the matching cube
    // LAYER (dst.layer = face index). SDL layer order is +X,-X,+Y,-Y,+Z,-Z.
    SDL_GPUCommandBuffer* cmd  = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass*      copy = SDL_BeginGPUCopyPass(cmd);
    for (Uint32 i = 0; i < 6; ++i) {
        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = transfer;
        src.offset          = i * faceBytes;
        src.pixels_per_row  = width;
        src.rows_per_layer  = height;
        SDL_GPUTextureRegion dst = {};
        dst.texture   = texture;
        dst.layer     = i;
        dst.w         = width;
        dst.h         = height;
        dst.d         = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, /*cycle=*/false);
    }
    SDL_EndGPUCopyPass(copy);
    SDL_GenerateMipmapsForGPUTexture(cmd, texture);  // fills all layers' mip levels
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    return texture;
}

bool GpuRenderer::loadCubemap(const std::array<std::string, 6>& facePaths) {
    // Load the six BMP faces the same way loadTexture loads one: SDL_LoadBMP →
    // convert to a known RGBA layout → repack to a tight pitch. All faces must share
    // one square size (the cube texture has a single width/height).
    std::array<std::vector<Uint8>, 6> packed;
    Uint32 width = 0, height = 0;
    for (Uint32 i = 0; i < 6; ++i) {
        SDL_Surface* surface = SDL_LoadBMP(facePaths[i].c_str());
        if (surface == nullptr) {
            KOI_ERROR("loadCubemap: SDL_LoadBMP('%s') failed: %s",
                      facePaths[i].c_str(), SDL_GetError());
            return false;
        }
        SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        if (rgba == nullptr) {
            KOI_ERROR("loadCubemap: SDL_ConvertSurface failed: %s", SDL_GetError());
            return false;
        }
        const Uint32 w = static_cast<Uint32>(rgba->w);
        const Uint32 h = static_cast<Uint32>(rgba->h);
        if (i == 0) {
            width = w;
            height = h;
        } else if (w != width || h != height) {
            KOI_ERROR("loadCubemap: face '%s' is %ux%u, expected %ux%u (all faces must match).",
                      facePaths[i].c_str(), w, h, width, height);
            SDL_DestroySurface(rgba);
            return false;
        }
        packed[i].resize(static_cast<size_t>(width) * height * 4);
        const Uint8* srcRows = static_cast<const Uint8*>(rgba->pixels);
        for (Uint32 row = 0; row < height; ++row) {
            std::memcpy(packed[i].data() + static_cast<size_t>(row) * width * 4,
                        srcRows + static_cast<size_t>(row) * static_cast<size_t>(rgba->pitch),
                        static_cast<size_t>(width) * 4);
        }
        SDL_DestroySurface(rgba);
    }

    const std::array<const void*, 6> faces = {
        packed[0].data(), packed[1].data(), packed[2].data(),
        packed[3].data(), packed[4].data(), packed[5].data()};
    SDL_GPUTexture* cube = uploadCubemap(faces, width, height);
    if (cube == nullptr) {
        return false;  // uploadCubemap already logged the cause
    }
    // Replace any previously loaded sky (harmless no-op on first load).
    if (skyboxCubemap_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, skyboxCubemap_);
    }
    skyboxCubemap_ = cube;
    KOI_INFO("Loaded skybox cubemap (%ux%u per face).", width, height);
    return true;
}

std::shared_ptr<Mesh> GpuRenderer::createMeshImpl(std::span<const Vertex> vertices,
                                                  const void* indexData, Uint32 indexBytes,
                                                  Uint32 indexCount,
                                                  SDL_GPUIndexElementSize elemSize) {
    // Upload both halves of the geometry through the same staging path the cube
    // used in earlier steps. size_bytes() is the element count times sizeof, i.e.
    // exactly the number of bytes to copy into each GPU buffer.
    SDL_GPUBuffer* vbo = uploadToGpuBuffer(SDL_GPU_BUFFERUSAGE_VERTEX,
                                           vertices.data(),
                                           static_cast<Uint32>(vertices.size_bytes()));
    SDL_GPUBuffer* ibo = uploadToGpuBuffer(SDL_GPU_BUFFERUSAGE_INDEX, indexData, indexBytes);

    if (vbo == nullptr || ibo == nullptr) {
        // Release whichever upload succeeded so a half-built mesh doesn't leak.
        if (vbo != nullptr) SDL_ReleaseGPUBuffer(device_, vbo);
        if (ibo != nullptr) SDL_ReleaseGPUBuffer(device_, ibo);
        KOI_ERROR("createMesh: geometry upload failed.");
        return nullptr;
    }

    // Hand the two buffers to a Mesh, which now owns them and will free them (via
    // the same device) when the last shared_ptr to it drops. The element size lets
    // the renderer bind the index buffer correctly (16- vs 32-bit).
    return std::make_shared<Mesh>(device_, vbo, ibo, indexCount, elemSize);
}

std::shared_ptr<Mesh> GpuRenderer::createMesh(std::span<const Vertex> vertices,
                                              std::span<const Uint16> indices) {
    return createMeshImpl(vertices, indices.data(),
                          static_cast<Uint32>(indices.size_bytes()),
                          static_cast<Uint32>(indices.size()),
                          SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

std::shared_ptr<Mesh> GpuRenderer::createMesh(std::span<const Vertex> vertices,
                                              std::span<const Uint32> indices) {
    return createMeshImpl(vertices, indices.data(),
                          static_cast<Uint32>(indices.size_bytes()),
                          static_cast<Uint32>(indices.size()),
                          SDL_GPU_INDEXELEMENTSIZE_32BIT);
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
                              const Vec3& cameraPos, std::span<const Light> lights,
                              const Mat4& lightViewProj) const {
    // Bind the one pipeline every mesh shares. Per-mesh vertex/index buffers AND
    // per-material textures are now bound inside recordNode, just before each node's
    // draw — different nodes use different materials, so binding can't be hoisted
    // out of the loop the way the single global texture was in Step 7.
    SDL_BindGPUGraphicsPipeline(pass, trianglePipeline_);

    // Bind the SHADOW MAP at fragment sampler slot 4 (Step 13 moved it past the four
    // per-material maps bound at slots 0–3 in recordNode). One shadow map for the whole
    // frame, so bind it once here.
    SDL_GPUTextureSamplerBinding shadowBinding = {};
    shadowBinding.texture = shadowMap_;
    shadowBinding.sampler = shadowSampler_;
    SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/4, &shadowBinding, /*num_bindings=*/1);

    // Pack the per-frame lighting environment (fragment set=3, binding 0): the ambient
    // fill, the eye (for specular), the sun's shadow matrix, and the ARRAY of active
    // lights. This C++ layout must match LightUBO in triangle.frag BYTE FOR BYTE — every
    // field is vec4-aligned and each GpuLight is exactly 4 vec4s (64 bytes), so std140
    // adds no hidden padding. Constant across every draw, so pushed ONCE here.
    struct GpuLight {
        float positionRange[4];   // xyz position, w range
        float directionType[4];   // xyz direction, w type (0=dir,1=point,2=spot)
        float colorIntensity[4];  // rgb color, a intensity
        float spotCutoffs[4];     // x innerCos, y outerCos
    };
    struct LightUniform {
        float    ambient[4];
        float    cameraPos[4];
        float    lightViewProj[16];
        Sint32   lightCount[4];        // ivec4: x = number of active lights
        GpuLight lights[MAX_LIGHTS];
    };

    LightUniform light = {};
    light.ambient[0] = 0.15f; light.ambient[1] = 0.15f; light.ambient[2] = 0.18f;  // soft fill
    light.cameraPos[0] = cameraPos.x;
    light.cameraPos[1] = cameraPos.y;
    light.cameraPos[2] = cameraPos.z;
    std::memcpy(light.lightViewProj, lightViewProj.m, sizeof(light.lightViewProj));

    // Copy the ENABLED lights into the fixed array, up to MAX_LIGHTS. Lights toggled
    // off from input are skipped, so the shader's loop never sees them. Index 0 stays
    // the sun (directional) whenever it's enabled — the only shadow caster.
    int packed = 0;
    for (const Light& l : lights) {
        if (packed >= MAX_LIGHTS) {
            break;
        }
        if (!l.enabled) {
            continue;
        }
        GpuLight& g = light.lights[packed];
        g.positionRange[0] = l.position.x;
        g.positionRange[1] = l.position.y;
        g.positionRange[2] = l.position.z;
        g.positionRange[3] = l.range;
        const Vec3 dir = normalize(l.direction);
        g.directionType[0] = dir.x;
        g.directionType[1] = dir.y;
        g.directionType[2] = dir.z;
        g.directionType[3] = static_cast<float>(static_cast<int>(l.type));
        g.colorIntensity[0] = l.color.x;
        g.colorIntensity[1] = l.color.y;
        g.colorIntensity[2] = l.color.z;
        g.colorIntensity[3] = l.intensity;
        g.spotCutoffs[0] = l.innerCutoffCos;
        g.spotCutoffs[1] = l.outerCutoffCos;
        ++packed;
    }
    light.lightCount[0] = packed;
    SDL_PushGPUFragmentUniformData(cmd, /*slot=*/0, &light, sizeof(light));

    // The projection depends only on the viewport, and the view only on the
    // camera, so combine them ONCE here. Each node then only adds one multiply by
    // its own world matrix: mvp = (proj * view) * world.
    const Mat4 projection = perspective(radians(60.0f), aspect, 0.1f, 100.0f);
    const Mat4 projView   = projection * view;

    // Walk the scene tree, drawing as we go. The nodes' world matrices were
    // computed by Node::updateWorldTransforms() before this render pass began.
    recordNode(cmd, pass, root, projView);

    // Draw the sky LAST (Step 14): with the depth buffer now holding the scene's
    // depths, the skybox's LEQUAL test + far-plane depth make it fill ONLY the
    // background pixels — no overdraw of shaded geometry. Skipped if no cubemap was
    // loaded or the sky is toggled off.
    if (skyboxCubemap_ != nullptr && skyboxEnabled_) {
        recordSkybox(cmd, pass, view, projection);
    }
}

void GpuRenderer::recordSkybox(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                               const Mat4& view, const Mat4& projection) const {
    // Strip the TRANSLATION from the view matrix, keeping only the camera's rotation,
    // so the sky turns with the camera yet never slides — it reads as infinitely far.
    // In our column-major Mat4 the translation is the last column (indices 12,13,14).
    Mat4 viewNoTranslation = view;
    viewNoTranslation.at(0, 3) = 0.0f;
    viewNoTranslation.at(1, 3) = 0.0f;
    viewNoTranslation.at(2, 3) = 0.0f;
    const Mat4 skyViewProj = projection * viewNoTranslation;

    // Binding a new pipeline resets the pass's bindings, so (re)bind everything the
    // sky draw needs: the pipeline, the cubemap sampler, the sky matrix, the cube.
    SDL_BindGPUGraphicsPipeline(pass, skyboxPipeline_);

    SDL_GPUTextureSamplerBinding sky = {};
    sky.texture = skyboxCubemap_;
    sky.sampler = skyboxSampler_;
    SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/0, &sky, /*num_bindings=*/1);

    SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, skyViewProj.m, sizeof(skyViewProj.m));

    SDL_GPUBufferBinding vb = {};
    vb.buffer = skyboxMesh_->vertexBuffer();
    SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vb, /*num_bindings=*/1);
    SDL_GPUBufferBinding ib = {};
    ib.buffer = skyboxMesh_->indexBuffer();
    SDL_BindGPUIndexBuffer(pass, &ib, skyboxMesh_->indexElementSize());
    SDL_DrawGPUIndexedPrimitives(pass, skyboxMesh_->indexCount(), /*num_instances=*/1,
                                 /*first_index=*/0, /*vertex_offset=*/0, /*first_instance=*/0);
}

void GpuRenderer::recordNode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                             const Node& node, const Mat4& projView) const {
    const Mesh*     mesh     = node.mesh();
    const Material* material = node.material();
    // A node is drawn only if it has BOTH a shape (mesh) and an appearance
    // (material). Pure group/pivot nodes have neither and just pass their transform
    // down to their children.
    if (mesh != nullptr && material != nullptr) {
        // Bind THIS material's four maps (+ the shared sampler) at fragment slots 0–3
        // for the upcoming draw: albedo, metallic-roughness, normal, AO. Each node can
        // use different maps, so they're (re)bound per draw — the same per-node rhythm
        // we already use for the mesh buffers below. A map the material omits falls
        // back to a neutral 1×1 texture (white for MR/AO, a flat normal), which makes
        // the shader reduce to the scalar-only look with no branching.
        const SDL_GPUTextureSamplerBinding maps[4] = {
            { material->albedo->handle(), sampler_ },
            { material->metalRough ? material->metalRough->handle() : whiteTex_,      sampler_ },
            { material->normalMap  ? material->normalMap->handle()  : flatNormalTex_, sampler_ },
            { material->ao         ? material->ao->handle()         : whiteTex_,      sampler_ },
        };
        SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/0, maps, /*num_bindings=*/4);

        // Push this material's PBR parameters (fragment set=3, binding 1): a vec4 with
        // metallic in x and roughness in y. Per draw, so each object can be a rough
        // dielectric or a polished metal independently of the next.
        const float materialParams[4] = { material->metallic, material->roughness,
                                          0.0f, 0.0f };
        SDL_PushGPUFragmentUniformData(cmd, /*slot=*/1, materialParams, sizeof(materialParams));

        // Bind THIS mesh's geometry. Different meshes (e.g. cube vs. ground plane)
        // live in different buffers, so we (re)bind per drawn node.
        SDL_GPUBufferBinding vertexBinding = {};
        vertexBinding.buffer = mesh->vertexBuffer();
        SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vertexBinding, /*num_bindings=*/1);
        SDL_GPUBufferBinding indexBinding = {};
        indexBinding.buffer = mesh->indexBuffer();
        SDL_BindGPUIndexBuffer(pass, &indexBinding, mesh->indexElementSize());

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
        // Neutral 1×1 fallback maps (Step 13). SDL_ReleaseGPUTexture tolerates null.
        SDL_ReleaseGPUTexture(device_, whiteTex_);
        SDL_ReleaseGPUTexture(device_, flatNormalTex_);
        whiteTex_ = flatNormalTex_ = nullptr;
        // Shadow-mapping resources (Step 9).
        if (shadowMap_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, shadowMap_);
            shadowMap_ = nullptr;
        }
        if (shadowSampler_ != nullptr) {
            SDL_ReleaseGPUSampler(device_, shadowSampler_);
            shadowSampler_ = nullptr;
        }
        if (shadowPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, shadowPipeline_);
            shadowPipeline_ = nullptr;
        }
        // Skybox resources (Step 14). The cube mesh is a member shared_ptr, so we must
        // drop it HERE (while the device is still alive) — member destructors run only
        // after this body, by which point SDL_DestroyGPUDevice below has torn the device
        // down and Mesh::~Mesh could no longer free its buffers.
        skyboxMesh_.reset();
        if (skyboxCubemap_ != nullptr) {
            SDL_ReleaseGPUTexture(device_, skyboxCubemap_);
            skyboxCubemap_ = nullptr;
        }
        if (skyboxSampler_ != nullptr) {
            SDL_ReleaseGPUSampler(device_, skyboxSampler_);
            skyboxSampler_ = nullptr;
        }
        if (skyboxPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, skyboxPipeline_);
            skyboxPipeline_ = nullptr;
        }
        // Post-processing resources (Step 10). SDL_ReleaseGPU* tolerate null, so the
        // lazily-created targets are safe to release even if a frame never ran.
        SDL_ReleaseGPUTexture(device_, sceneHdr_);
        SDL_ReleaseGPUTexture(device_, ldr_);
        SDL_ReleaseGPUTexture(device_, bloomA_);
        SDL_ReleaseGPUTexture(device_, bloomB_);
        sceneHdr_ = ldr_ = bloomA_ = bloomB_ = nullptr;
        if (postSampler_ != nullptr) {
            SDL_ReleaseGPUSampler(device_, postSampler_);
            postSampler_ = nullptr;
        }
        if (brightPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, brightPipeline_);
            brightPipeline_ = nullptr;
        }
        if (blurPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, blurPipeline_);
            blurPipeline_ = nullptr;
        }
        if (compositePipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, compositePipeline_);
            compositePipeline_ = nullptr;
        }
        if (fxaaPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, fxaaPipeline_);
            fxaaPipeline_ = nullptr;
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
                              const Node& sceneRoot, const Vec3& cameraPos,
                              std::span<const Light> lights, const PostSettings& post) {
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
    // skip rendering this frame but still submit the (empty) buffer.
    if (swapchainTexture != nullptr) {
        // The whole pipeline now runs through one shared helper: shadow pass → scene
        // into the off-screen HDR target → post-processing chain whose FINAL pass
        // (FXAA) writes the displayable image straight into the swapchain texture.
        // The aspect ratio comes from the actual swapchain size, so the image is never
        // stretched and adapts automatically when the window is resized.
        renderSceneAndPost(cmd, swapchainTexture, width, height, view, sceneRoot,
                           cameraPos, lights, clearColor, post);
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
                               const Vec3& cameraPos, std::span<const Light> lights,
                               const PostSettings& post) {
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

    // 3. Record the SAME full pipeline the live path uses — shadow pass → scene into
    //    the HDR target → the post-processing chain whose FXAA pass writes the final
    //    image into our off-screen `target`. Sharing renderSceneAndPost means the BMP
    //    matches what the window shows (and reflects `post`). Deterministic: nothing
    //    here is time-based, so the supplied `view` (the camera's default pose) renders
    //    the same frame every time.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    renderSceneAndPost(cmd, target, kWidth, kHeight, view, sceneRoot, cameraPos,
                       lights, clearColor, post);

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
