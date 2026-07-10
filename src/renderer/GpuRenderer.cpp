#include "renderer/GpuRenderer.hpp"

#include <array>    // std::array (the six cubemap face paths / faces)
#include <cstddef>  // offsetof
#include <cstring>  // std::memcpy
#include <memory>   // std::make_shared
#include <string>   // std::string (texture path extension dispatch)
#include <vector>   // std::vector (repack texture rows to a tight pitch)

#include "core/Log.hpp"
#include "math/Mat4.hpp"
#include "renderer/DebugDraw.hpp"   // DebugVertex — the immediate-mode debug lines (Step 22)
#include "renderer/Mesh.hpp"
#include "renderer/Primitives.hpp"  // makeCubeMesh — reused as the skybox cube
#include "renderer/Shader.hpp"
#include "renderer/Texture.hpp"
#include "renderer/Vertex.hpp"
#include "scene/Node.hpp"

// stb_image DECLARATIONS only — the bodies are compiled once in ModelLoader.cpp
// (which #defines STB_IMAGE_IMPLEMENTATION). We use it here to decode PNG/JPG in
// loadTexture, since SDL_LoadBMP handles only BMP. Fetched into MODELS_DIR (a
// private include dir on koi_core) alongside cgltf.h — see CMakeLists.txt (Step 16).
#include "stb_image.h"

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

// Image-based lighting (Step 15) bake sizes. These maps can be small: diffuse irradiance
// is extremely low-frequency, and the specular prefilter's roughness levels live in mips
// (mip 0 = mirror-sharp, the last mip = fully rough). kPrefilterMipLevels MUST match
// MAX_REFLECTION_LOD+1 in triangle.frag (roughness maps onto [0 .. mips-1]).
constexpr Uint32 kIrradianceSize     = 32;   // diffuse irradiance cube face
constexpr Uint32 kPrefilterSize      = 128;  // specular prefilter cube base face
constexpr Uint32 kPrefilterMipLevels = 5;    // roughness 0..1 across 5 mips
constexpr Uint32 kBrdfLutSize        = 512;  // 2D BRDF integration LUT
// The LUT holds a scale+bias per texel — two channels, floating point for precision.
constexpr SDL_GPUTextureFormat kBrdfLutFormat = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
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

    // ------------------------------------------------------------------------
    //  7b. Create the image-based-lighting resources (Step 15): the three bake
    //      pipelines, the trilinear cubemap sampler, and the irradiance / prefilter /
    //      BRDF-LUT targets. The environment-independent BRDF LUT is baked right away;
    //      the two environment convolutions are baked once a cubemap is loaded
    //      (loadCubemap → bakeIbl). If this fails, isValid() reports it.
    // ------------------------------------------------------------------------
    if (!createIblResources()) {
        return;
    }

    // ------------------------------------------------------------------------
    //  8. Create the debug-line pipeline (Step 22): a minimal unlit pipeline that
    //     overlays wireframes (AABBs, the camera frustum, light icons) drawn from
    //     the FrameView's per-frame debug lines. If this fails, isValid() reports it.
    // ------------------------------------------------------------------------
    if (!createDebugPipeline()) {
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
    // Step 16: the fragment shader now reads NINE samplers — the four per-material maps
    // (albedo, metallic-roughness, normal, AO), the shared shadow map, the three IBL maps
    // (irradiance cube, prefilter cube, BRDF LUT), and the per-material EMISSIVE map.
    SDL_GPUShader* fragmentShader = loadShader(device_, "triangle.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, /*numUniformBuffers=*/2, /*numSamplers=*/9);
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
    // Blending and face culling stay at defaults (off) for the OPAQUE pipeline: the
    // depth test alone resolves visibility. (The transparent pipeline below flips
    // blending on.) Culling is a later optimization.

    trianglePipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);

    // ------------------------------------------------------------------------
    //  The TRANSPARENT pipeline (Step 21) — same shaders + layout, two changes:
    //   * BLENDING ON, the "over" operator: out = src·srcAlpha + dst·(1-srcAlpha).
    //     That composites a translucent fragment on top of whatever colour is already
    //     in the HDR target. (SDL bakes blend state into the immutable pipeline, so it
    //     can't be a per-draw toggle — hence a second pipeline object.)
    //   * DEPTH-WRITE OFF (test still LESS): a translucent surface is still occluded by
    //     nearer opaque geometry, but leaves the depth buffer untouched so the
    //     back-to-front translucent draws behind it can blend through instead of being
    //     depth-rejected. Correct ordering comes from sorting the queue, not from depth.
    // Reuse pipelineInfo wholesale; only the color target's blend state and the
    // depth-write flag differ.
    SDL_GPUColorTargetDescription transparentColorTargetDesc = colorTargetDesc;
    SDL_GPUColorTargetBlendState& blend = transparentColorTargetDesc.blend_state;
    blend.enable_blend          = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op        = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo transparentInfo = pipelineInfo;
    transparentInfo.target_info.color_target_descriptions = &transparentColorTargetDesc;
    transparentInfo.depth_stencil_state.enable_depth_write = false;  // test stays on (LESS)

    transparentPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &transparentInfo);

    // Both pipelines copy what they need from the shaders, so release the shader
    // objects now — the pipelines keep working without them.
    SDL_ReleaseGPUShader(device_, vertexShader);
    SDL_ReleaseGPUShader(device_, fragmentShader);

    if (trianglePipeline_ == nullptr) {
        KOI_ERROR("Failed to create graphics pipeline: %s", SDL_GetError());
        return false;
    }
    if (transparentPipeline_ == nullptr) {
        KOI_ERROR("Failed to create transparent graphics pipeline: %s", SDL_GetError());
        return false;
    }
    return true;
}

bool GpuRenderer::createDebugPipeline() {
    // The debug overlay's shaders: a world-space position + flat colour in, a solid
    // colour out (no lighting, no textures). The vertex shader declares ONE uniform
    // buffer (the view-projection); the fragment shader reads nothing.
    SDL_GPUShader* vertexShader = loadShader(device_, "debug_line.vert",
                                             SDL_GPU_SHADERSTAGE_VERTEX, /*numUniformBuffers=*/1);
    SDL_GPUShader* fragmentShader = loadShader(device_, "debug_line.frag",
                                               SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (vertexShader == nullptr || fragmentShader == nullptr) {
        if (vertexShader != nullptr)   SDL_ReleaseGPUShader(device_, vertexShader);
        if (fragmentShader != nullptr) SDL_ReleaseGPUShader(device_, fragmentShader);
        return false;
    }

    // Debug lines are drawn INTO the HDR scene target (so they show up in
    // KOI_CAPTURE frames too), so the pipeline's colour format is the HDR one — the
    // same as the main pass. depthFormat_ was chosen when the triangle pipeline built.
    SDL_GPUColorTargetDescription colorTargetDesc = {};
    colorTargetDesc.format = kSceneHdrFormat;

    // Vertex layout: one buffer at DebugVertex stride with two FLOAT3 attributes —
    // position at offset 0, colour right after. This MUST match koi::DebugVertex and
    // debug_line.vert's `layout(location = N) in` inputs.
    SDL_GPUVertexBufferDescription vertexBufferDesc = {};
    vertexBufferDesc.slot       = 0;
    vertexBufferDesc.pitch      = static_cast<Uint32>(sizeof(DebugVertex));
    vertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    const SDL_GPUVertexAttribute vertexAttributes[2] = {
        { /*location=*/0, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          offsetof(DebugVertex, position) },
        { /*location=*/1, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          offsetof(DebugVertex, color) },
    };

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader   = vertexShader;
    info.fragment_shader = fragmentShader;
    info.vertex_input_state.vertex_buffer_descriptions = &vertexBufferDesc;
    info.vertex_input_state.num_vertex_buffers         = 1;
    info.vertex_input_state.vertex_attributes          = vertexAttributes;
    info.vertex_input_state.num_vertex_attributes      = 2;
    // LINELIST: every TWO vertices form one independent segment — the reason
    // DebugDraw stores its geometry as vertex pairs.
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;

    // Depth TEST on so a line behind opaque geometry is correctly hidden (a normal
    // poking through a wall shouldn't show); depth WRITE off so this throwaway
    // overlay never stamps the scene's depth buffer. LEQUAL lets a line lying
    // exactly on a surface still draw.
    info.depth_stencil_state.enable_depth_test  = true;
    info.depth_stencil_state.enable_depth_write = false;
    info.depth_stencil_state.compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    info.target_info.num_color_targets         = 1;
    info.target_info.color_target_descriptions = &colorTargetDesc;
    info.target_info.has_depth_stencil_target  = true;
    info.target_info.depth_stencil_format      = depthFormat_;

    debugLinePipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);

    SDL_ReleaseGPUShader(device_, vertexShader);
    SDL_ReleaseGPUShader(device_, fragmentShader);

    if (debugLinePipeline_ == nullptr) {
        KOI_ERROR("Failed to create debug-line pipeline: %s", SDL_GetError());
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

// The six view matrices used to bake a cubemap: a camera at the origin looking down each
// of the ±X/±Y/±Z axes, in SDL's cube layer order (+X,-X,+Y,-Y,+Z,-Z). The up-vectors are
// the standard cube-capture convention (the horizontal faces flip up so the six renders
// line up seamlessly with how a samplerCube later reads them). Paired with a 90° FOV
// projection, each render exactly fills one face.
static std::array<Mat4, 6> cubeCaptureViews() {
    const Vec3 o{0.0f, 0.0f, 0.0f};
    return {
        lookAt(o, Vec3{ 1.0f,  0.0f,  0.0f}, Vec3{0.0f, -1.0f,  0.0f}),  // +X
        lookAt(o, Vec3{-1.0f,  0.0f,  0.0f}, Vec3{0.0f, -1.0f,  0.0f}),  // -X
        lookAt(o, Vec3{ 0.0f,  1.0f,  0.0f}, Vec3{0.0f,  0.0f,  1.0f}),  // +Y
        lookAt(o, Vec3{ 0.0f, -1.0f,  0.0f}, Vec3{0.0f,  0.0f, -1.0f}),  // -Y
        lookAt(o, Vec3{ 0.0f,  0.0f,  1.0f}, Vec3{0.0f, -1.0f,  0.0f}),  // +Z
        lookAt(o, Vec3{ 0.0f,  0.0f, -1.0f}, Vec3{0.0f, -1.0f,  0.0f}),  // -Z
    };
}

bool GpuRenderer::createIblResources() {
    // The RG float LUT format must be a usable color target on this device.
    if (!SDL_GPUTextureSupportsFormat(device_, kBrdfLutFormat, SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                      SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        KOI_ERROR("IBL: BRDF-LUT format (R16G16_FLOAT) unsupported as a sampleable target.");
        return false;
    }

    // The two cube-bake pipelines share a vertex layout: position only, at the full Vertex
    // stride (we reuse the skybox cube mesh — the shadow/skybox precedent). They render
    // into an HDR cube face with NO depth buffer.
    SDL_GPUVertexBufferDescription vbDesc = {};
    vbDesc.slot       = 0;
    vbDesc.pitch      = static_cast<Uint32>(sizeof(Vertex));
    vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    const SDL_GPUVertexAttribute posAttr = {
        /*location=*/0, /*buffer_slot=*/0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
        offsetof(Vertex, position)};
    SDL_GPUColorTargetDescription hdrTarget = {};
    hdrTarget.format = kSceneHdrFormat;

    auto buildCubePipeline = [&](const char* frag, Uint32 numFragUniforms) -> SDL_GPUGraphicsPipeline* {
        SDL_GPUShader* vs = loadShader(device_, "ibl_cube.vert", SDL_GPU_SHADERSTAGE_VERTEX,
                                       /*numUniformBuffers=*/1);
        SDL_GPUShader* fs = loadShader(device_, frag, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                       numFragUniforms, /*numSamplers=*/1);
        if (vs == nullptr || fs == nullptr) {
            if (vs != nullptr) SDL_ReleaseGPUShader(device_, vs);
            if (fs != nullptr) SDL_ReleaseGPUShader(device_, fs);
            return nullptr;
        }
        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader   = vs;
        info.fragment_shader = fs;
        info.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        info.vertex_input_state.num_vertex_buffers         = 1;
        info.vertex_input_state.vertex_attributes          = &posAttr;
        info.vertex_input_state.num_vertex_attributes      = 1;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        // We render the cube from INSIDE, so disable face culling — otherwise the faces we
        // actually look at (back faces from the origin) would be discarded.
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.target_info.num_color_targets         = 1;
        info.target_info.color_target_descriptions = &hdrTarget;
        info.target_info.has_depth_stencil_target  = false;
        SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(device_, &info);
        SDL_ReleaseGPUShader(device_, vs);
        SDL_ReleaseGPUShader(device_, fs);
        return pipe;
    };

    iblIrradiancePipeline_ = buildCubePipeline("irradiance_convolution.frag", /*numFragUniforms=*/0);
    iblPrefilterPipeline_  = buildCubePipeline("prefilter_env.frag",          /*numFragUniforms=*/1);
    if (iblIrradiancePipeline_ == nullptr || iblPrefilterPipeline_ == nullptr) {
        KOI_ERROR("IBL: failed to create a cube-bake pipeline: %s", SDL_GetError());
        return false;
    }

    // The BRDF-LUT pipeline: a fullscreen pass (fullscreen.vert, no vertex input, no depth)
    // into the RG float target. brdf_lut.frag reads nothing (no uniforms, no samplers).
    {
        SDL_GPUShader* vs = loadShader(device_, "fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX);
        SDL_GPUShader* fs = loadShader(device_, "brdf_lut.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
                                       /*numUniformBuffers=*/0, /*numSamplers=*/0);
        if (vs == nullptr || fs == nullptr) {
            if (vs != nullptr) SDL_ReleaseGPUShader(device_, vs);
            if (fs != nullptr) SDL_ReleaseGPUShader(device_, fs);
            return false;
        }
        SDL_GPUColorTargetDescription lutTarget = {};
        lutTarget.format = kBrdfLutFormat;
        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader   = vs;
        info.fragment_shader = fs;
        info.vertex_input_state.num_vertex_buffers    = 0;
        info.vertex_input_state.num_vertex_attributes = 0;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.target_info.num_color_targets         = 1;
        info.target_info.color_target_descriptions = &lutTarget;
        info.target_info.has_depth_stencil_target  = false;
        iblBrdfPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &info);
        SDL_ReleaseGPUShader(device_, vs);
        SDL_ReleaseGPUShader(device_, fs);
        if (iblBrdfPipeline_ == nullptr) {
            KOI_ERROR("IBL: failed to create BRDF-LUT pipeline: %s", SDL_GetError());
            return false;
        }
    }

    // The sampler the IBL maps are read through: LINEAR + trilinear (LINEAR mipmap mode) so
    // the prefilter's roughness mips blend smoothly under textureLod, and CLAMP_TO_EDGE so
    // cube faces meet without seams. (postSampler_ uses NEAREST mipmaps, so it can't serve.)
    SDL_GPUSamplerCreateInfo si = {};
    si.min_filter     = SDL_GPU_FILTER_LINEAR;
    si.mag_filter     = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    iblSampler_ = SDL_CreateGPUSampler(device_, &si);
    if (iblSampler_ == nullptr) {
        KOI_ERROR("IBL: failed to create sampler: %s", SDL_GetError());
        return false;
    }

    // Allocate the target textures (both cubes need COLOR_TARGET so we can render into
    // their faces/mips, plus SAMPLER so the main pass can read them).
    auto makeCube = [&](Uint32 size, Uint32 mips) -> SDL_GPUTexture* {
        SDL_GPUTextureCreateInfo ti = {};
        ti.type                 = SDL_GPU_TEXTURETYPE_CUBE;
        ti.format               = kSceneHdrFormat;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width                = size;
        ti.height               = size;
        ti.layer_count_or_depth = 6;
        ti.num_levels           = mips;
        return SDL_CreateGPUTexture(device_, &ti);
    };
    iblIrradiance_ = makeCube(kIrradianceSize, /*mips=*/1);
    iblPrefilter_  = makeCube(kPrefilterSize, kPrefilterMipLevels);
    {
        SDL_GPUTextureCreateInfo ti = {};
        ti.type                 = SDL_GPU_TEXTURETYPE_2D;
        ti.format               = kBrdfLutFormat;
        ti.usage                = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width                = kBrdfLutSize;
        ti.height               = kBrdfLutSize;
        ti.layer_count_or_depth = 1;
        ti.num_levels           = 1;
        iblBrdfLut_ = SDL_CreateGPUTexture(device_, &ti);
    }
    if (iblIrradiance_ == nullptr || iblPrefilter_ == nullptr || iblBrdfLut_ == nullptr) {
        KOI_ERROR("IBL: failed to create a target texture: %s", SDL_GetError());
        return false;
    }

    // Bake the BRDF LUT now, once: it's a pure function of (N·V, roughness) — independent
    // of the environment — so it never needs re-baking, unlike the two cube convolutions.
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUColorTargetInfo ci = {};
    ci.texture     = iblBrdfLut_;
    ci.clear_color = kPostClearBlack;
    ci.load_op     = SDL_GPU_LOADOP_CLEAR;
    ci.store_op    = SDL_GPU_STOREOP_STORE;
    ci.cycle       = false;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, iblBrdfPipeline_);
    drawFullscreen(pass);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

void GpuRenderer::bakeCubeFace(SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline* pipeline,
                               SDL_GPUTexture* target, Uint32 face, Uint32 mip, Uint32 size,
                               const Mat4& captureViewProj, const float* prefilterParams) const {
    // Target exactly ONE face+mip subresource of the cube. layer_or_depth_plane selects the
    // cube face; mip_level selects the roughness level. No cycle: we write the real texture.
    SDL_GPUColorTargetInfo ci = {};
    ci.texture              = target;
    ci.mip_level            = mip;
    ci.layer_or_depth_plane = face;
    ci.clear_color          = kPostClearBlack;
    ci.load_op              = SDL_GPU_LOADOP_CLEAR;
    ci.store_op             = SDL_GPU_STOREOP_STORE;
    ci.cycle                = false;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);

    // The viewport must match this mip's dimensions (a prefilter mip is smaller than base).
    SDL_GPUViewport vp = {0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size),
                          0.0f, 1.0f};
    SDL_SetGPUViewport(pass, &vp);

    SDL_BindGPUGraphicsPipeline(pass, pipeline);

    // Source environment: the loaded skybox cubemap at fragment sampler slot 0.
    SDL_GPUTextureSamplerBinding env = {};
    env.texture = skyboxCubemap_;
    env.sampler = skyboxSampler_;
    SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/0, &env, /*num_bindings=*/1);

    // Vertex uniform: this face's capture view-projection.
    SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, captureViewProj.m, sizeof(captureViewProj.m));
    // Fragment uniform: the prefilter's {roughness, envResolution}; irradiance takes none.
    if (prefilterParams != nullptr) {
        SDL_PushGPUFragmentUniformData(cmd, /*slot=*/0, prefilterParams, sizeof(float) * 4);
    }

    SDL_GPUBufferBinding vb = {};
    vb.buffer = skyboxMesh_->vertexBuffer();
    SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vb, /*num_bindings=*/1);
    SDL_GPUBufferBinding ib = {};
    ib.buffer = skyboxMesh_->indexBuffer();
    SDL_BindGPUIndexBuffer(pass, &ib, skyboxMesh_->indexElementSize());
    SDL_DrawGPUIndexedPrimitives(pass, skyboxMesh_->indexCount(), /*num_instances=*/1,
                                 /*first_index=*/0, /*vertex_offset=*/0, /*first_instance=*/0);
    SDL_EndGPURenderPass(pass);
}

void GpuRenderer::bakeIbl() {
    // Needs both a loaded environment and the pipelines (created in the ctor).
    if (skyboxCubemap_ == nullptr || iblIrradiancePipeline_ == nullptr ||
        iblPrefilterPipeline_ == nullptr) {
        return;
    }

    const std::array<Mat4, 6> views = cubeCaptureViews();
    // 90° FOV so one face fills the render exactly; near/far are arbitrary (directions only).
    const Mat4 proj = perspective(radians(90.0f), 1.0f, 0.1f, 10.0f);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);

    // 1. Diffuse irradiance: one render per face into mip 0. No per-face parameters.
    for (Uint32 face = 0; face < 6; ++face) {
        const Mat4 vp = proj * views[face];
        bakeCubeFace(cmd, iblIrradiancePipeline_, iblIrradiance_, face, /*mip=*/0,
                     kIrradianceSize, vp, /*prefilterParams=*/nullptr);
    }

    // 2. Specular prefilter: one render per (face, mip). Mip m stores roughness m/(N-1);
    //    a smaller mip means a rougher (blurrier) reflection.
    for (Uint32 mip = 0; mip < kPrefilterMipLevels; ++mip) {
        const Uint32 mipSize = kPrefilterSize >> mip;
        const float roughness = (kPrefilterMipLevels <= 1)
                                    ? 0.0f
                                    : static_cast<float>(mip) /
                                          static_cast<float>(kPrefilterMipLevels - 1);
        // {roughness, source-env face resolution, 0, 0} — the shader uses the resolution to
        // pick a source mip per sample and suppress fireflies.
        const float params[4] = {roughness, static_cast<float>(skyboxFaceSize_), 0.0f, 0.0f};
        for (Uint32 face = 0; face < 6; ++face) {
            const Mat4 vp = proj * views[face];
            bakeCubeFace(cmd, iblPrefilterPipeline_, iblPrefilter_, face, mip, mipSize, vp,
                         params);
        }
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    iblReady_ = true;
    KOI_INFO("Baked IBL from skybox: irradiance %ux%u, prefilter %ux%u (%u mips).",
             kIrradianceSize, kIrradianceSize, kPrefilterSize, kPrefilterSize,
             kPrefilterMipLevels);
}

void GpuRenderer::renderShadowPass(SDL_GPUCommandBuffer* cmd,
                                   const std::vector<RenderItem>& queue,
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

    // Draw EVERY item's depth (material/texture irrelevant here — depth only). This
    // loop is deliberately NOT frustum-culled to the camera: geometry the camera
    // can't see may still cast a shadow onto geometry it can. Culling here would
    // make shadows blink as their casters cross the camera frustum edge.
    for (const RenderItem& item : queue) {
        SDL_GPUBufferBinding vertexBinding = {};
        vertexBinding.buffer = item.mesh->vertexBuffer();
        SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vertexBinding, /*num_bindings=*/1);
        SDL_GPUBufferBinding indexBinding = {};
        indexBinding.buffer = item.mesh->indexBuffer();
        SDL_BindGPUIndexBuffer(pass, &indexBinding, item.mesh->indexElementSize());

        const Mat4 lightMvp = lightViewProj * item.world;
        SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, &lightMvp, sizeof(lightMvp));
        SDL_DrawGPUIndexedPrimitives(pass, item.mesh->indexCount(), /*num_instances=*/1,
                                     /*first_index=*/0, /*vertex_offset=*/0,
                                     /*first_instance=*/0);
    }
    SDL_EndGPURenderPass(pass);
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
                                     const SDL_FColor& clearColor, const PostSettings& post,
                                     std::span<const DebugVertex> debugLines) {
    if (!ensureSceneTargets(width, height)) {
        return;  // ensureSceneTargets logged; skip this frame rather than crash
    }

    // Upload this frame's debug lines (Step 22) BEFORE any render pass begins on
    // `cmd`: uploadDebugLines does its copy on its own command buffer, and doing it
    // here keeps the transient vertex buffer ready for recordScene's overlay draw.
    // (Empty when debug draw is off, so this is a cheap no-op in the common case.)
    uploadDebugLines(debugLines);

    // TRAVERSE → LIST (Step 20). Flatten the scene graph into a flat draw list ONCE,
    // up front, so both passes below iterate the list instead of re-walking the tree.
    // `renderQueue_` is a reused member, so this reuses last frame's capacity. World
    // matrices were cached by the app's updateWorldTransforms() before we were called.
    renderQueue_.clear();
    buildRenderQueue(sceneRoot, renderQueue_);

    // The shadow map follows the SUN — the directional light at index 0 (by
    // convention). Its direction drives both passes so the shadows line up with the
    // shading, even as the sun is reconfigured. (Only this one light casts a shadow.)
    const Vec3 sunDir = (!lights.empty() && lights[0].type == LightType::Directional)
                            ? lights[0].direction
                            : kFallbackSunDir;

    // PASS 1 — shadow depth from the sun's view. Draws the WHOLE queue (no camera
    // culling: an off-screen caster can still shadow something in view).
    const Mat4 lightViewProj = computeLightViewProj(sunDir);
    renderShadowPass(cmd, renderQueue_, lightViewProj);

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
    recordScene(cmd, pass, renderQueue_, view, aspect, cameraPos, lights, lightViewProj);
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
                                                Uint32 height, bool withMips, bool srgb) {
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
    // sRGB colour maps use the _SRGB variant so the GPU un-gammas each texel to linear
    // when sampled (shading is done in linear light); DATA maps and the BMP path stay
    // plain UNORM. Same byte layout either way — only the sampling interpretation differs.
    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type                 = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format               = srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
                                        : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;  // 8-bit RGBA
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
    skyboxFaceSize_ = width;  // the prefilter bake needs the source face resolution
    KOI_INFO("Loaded skybox cubemap (%ux%u per face).", width, height);

    // Bake image-based lighting from this sky (Step 15): the diffuse irradiance and
    // specular prefilter cubemaps. Done here so a sky reload automatically re-bakes.
    bakeIbl();
    return true;
}

std::shared_ptr<Mesh> GpuRenderer::createMeshImpl(std::span<const Vertex> vertices,
                                                  const void* indexData, Uint32 indexBytes,
                                                  Uint32 indexCount,
                                                  SDL_GPUIndexElementSize elemSize) {
    // Compute the mesh's model-space bounding box from the CPU-side vertices NOW,
    // before they're gone (the vertices only exist here — the Mesh keeps just the
    // GPU buffers). This is the box frustum culling later transforms + tests
    // (Step 20). Pure CPU work, so it's a no-op on the GPU path.
    const Aabb localBounds = computeLocalBounds(vertices);

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
    return std::make_shared<Mesh>(device_, vbo, ibo, indexCount, localBounds, elemSize);
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

std::shared_ptr<Texture> GpuRenderer::createTextureFromRGBA(const void* pixels,
                                                            Uint32 width, Uint32 height,
                                                            bool srgb, bool withMips) {
    // The shared tail of every texture load: upload tightly-packed RGBA into a GPU
    // texture (with mipmaps + the sRGB choice) and wrap it in a Texture, which now
    // owns it and frees it via the same device when its last shared_ptr drops.
    SDL_GPUTexture* gpuTex = uploadToGpuTexture(pixels, width, height, withMips, srgb);
    if (gpuTex == nullptr) {
        return nullptr;  // uploadToGpuTexture already logged the cause
    }
    return std::make_shared<Texture>(device_, gpuTex, width, height);
}

std::shared_ptr<Texture> GpuRenderer::loadTexture(const char* path, bool srgb) {
    // Decode the file to tightly-packed RGBA, then hand it to createTextureFromRGBA.
    // BMP goes through SDL (no extra dependency); PNG/JPG (used by glTF — Step 16)
    // through stb_image. We dispatch on the extension so the common BMP path is byte-
    // for-byte the same as before.
    const std::string p = path;
    const bool isBmp = p.size() >= 4 && (p.compare(p.size() - 4, 4, ".bmp") == 0 ||
                                         p.compare(p.size() - 4, 4, ".BMP") == 0);

    if (isBmp) {
        // 1. SDL_LoadBMP reads BMP with no extra library and returns a CPU surface.
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
        //    (bytes per row) can include padding; the GPU uploader wants no gaps.
        std::vector<Uint8> packed(static_cast<size_t>(width) * height * 4);
        const Uint8* srcRows = static_cast<const Uint8*>(rgba->pixels);
        for (Uint32 row = 0; row < height; ++row) {
            std::memcpy(packed.data() + static_cast<size_t>(row) * width * 4,
                        srcRows + static_cast<size_t>(row) * static_cast<size_t>(rgba->pitch),
                        static_cast<size_t>(width) * 4);
        }
        SDL_DestroySurface(rgba);
        std::shared_ptr<Texture> tex = createTextureFromRGBA(packed.data(), width, height, srgb);
        if (tex) { KOI_INFO("Loaded texture '%s' (%ux%u%s).", path, width, height,
                            srgb ? ", sRGB" : ""); }
        return tex;
    }

    // Non-BMP: stb_image decodes PNG/JPG/etc. to RGBA (forcing 4 channels).
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(path, &w, &h, &channels, /*desired_channels=*/4);
    if (data == nullptr) {
        KOI_ERROR("loadTexture: stbi_load('%s') failed: %s", path, stbi_failure_reason());
        return nullptr;
    }
    std::shared_ptr<Texture> tex = createTextureFromRGBA(
        data, static_cast<Uint32>(w), static_cast<Uint32>(h), srgb);
    stbi_image_free(data);
    if (tex) { KOI_INFO("Loaded texture '%s' (%dx%d%s).", path, w, h, srgb ? ", sRGB" : ""); }
    return tex;
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

void GpuRenderer::bindFrameLighting(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                                    const Vec3& cameraPos, std::span<const Light> lights,
                                    const Mat4& lightViewProj) const {
    // Bind the SHADOW MAP at fragment sampler slot 4 (Step 13 moved it past the four
    // per-material maps bound at slots 0–3 in submitItem). One shadow map for the whole
    // frame. These sampler binds must be re-run after every scene pipeline bind — a
    // pipeline switch resets the pass's sampler bindings — which is why they live here
    // and this helper is called once per pass (opaque, then transparent).
    SDL_GPUTextureSamplerBinding shadowBinding = {};
    shadowBinding.texture = shadowMap_;
    shadowBinding.sampler = shadowSampler_;
    SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/4, &shadowBinding, /*num_bindings=*/1);

    // Bind the IBL maps at fragment sampler slots 5–7 (Step 15): the diffuse irradiance
    // cube, the specular prefilter cube, and the environment BRDF LUT. They always exist
    // (created at init), so the bind is unconditional; the shader only READS them when the
    // IBL flag in ambient.w is set.
    const SDL_GPUTextureSamplerBinding iblBindings[3] = {
        {iblIrradiance_, iblSampler_},
        {iblPrefilter_,  iblSampler_},
        {iblBrdfLut_,    iblSampler_},
    };
    SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/5, iblBindings, /*num_bindings=*/3);

    // Pack the per-frame lighting environment (fragment set=3, binding 0): the ambient
    // fill, the eye (for specular), the sun's shadow matrix, and the ARRAY of active
    // lights. This C++ layout must match LightUBO in triangle.frag BYTE FOR BYTE — every
    // field is vec4-aligned and each GpuLight is exactly 4 vec4s (64 bytes), so std140
    // adds no hidden padding. (Pushed uniform data actually survives a pipeline switch,
    // but re-pushing per pass is cheap and keeps all per-frame lighting set up here.)
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
    // ambient.w is the IBL switch the shader reads (Step 15): 1 → use the baked environment
    // maps for ambient, 0 → the flat constant fill above (the Step 12 look). On only once a
    // cubemap has actually been baked AND the runtime toggle is on.
    light.ambient[3] = (iblEnabled_ && iblReady_) ? 1.0f : 0.0f;
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
}

void GpuRenderer::recordScene(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                              const std::vector<RenderItem>& queue, const Mat4& view,
                              float aspect, const Vec3& cameraPos,
                              std::span<const Light> lights,
                              const Mat4& lightViewProj) const {
    // The projection depends only on the viewport, and the view only on the
    // camera, so combine them ONCE here. Each item then only adds one multiply by
    // its own world matrix: mvp = (proj * view) * world.
    const Mat4 projection = perspective(radians(60.0f), aspect, 0.1f, 100.0f);
    const Mat4 projView   = projection * view;

    // Remember this frame's camera view-projection (Step 22) so the app can freeze
    // it and draw the culling frustum as a wireframe (see lastCameraViewProjection).
    lastCameraViewProj_ = projView;

    // CULL (Step 20). Extract the six camera-frustum planes from the SAME projView the
    // shader uses (so what we cull matches what's on screen), then keep only the items
    // whose world bounds intersect it. With culling off, every item survives (the
    // pre-Step-20 behaviour). visibleItems_ is a reused scratch vector.
    const Frustum cameraFrustum = Frustum::fromViewProjection(projView);
    visibleItems_.clear();
    for (const RenderItem& item : queue) {
        if (!frustumCullingEnabled_ || cameraFrustum.intersectsAabb(item.worldBounds)) {
            visibleItems_.push_back(&item);
        }
    }

    // Log what culling bought this frame (throttled to avoid spamming the console).
    // Off-screen objects never reach a draw call — CPU submit + GPU work both saved.
    if (frustumCullingEnabled_ && !queue.empty()) {
        static Uint64 tick = 0;
        if ((tick++ % 120) == 0) {
            KOI_DEBUG("Frustum culling: drew %zu / %zu (culled %zu)",
                      visibleItems_.size(), queue.size(),
                      queue.size() - visibleItems_.size());
        }
    }

    // PARTITION (Step 21). Split the survivors into opaque + transparent lists; the
    // transparent list is ordered back-to-front (unless the runtime toggle is off) so
    // alpha blending composites correctly. Reused scratch vectors, like visibleItems_.
    partitionByBlend(visibleItems_, cameraPos, transparentSortEnabled_,
                     opaqueItems_, transparentItems_);

    // --- OPAQUE PASS: depth-write ON, no blending ----------------------------------
    // Order among opaque items doesn't matter — the depth buffer resolves visibility.
    SDL_BindGPUGraphicsPipeline(pass, trianglePipeline_);
    bindFrameLighting(cmd, pass, cameraPos, lights, lightViewProj);
    for (const RenderItem* item : opaqueItems_) {
        submitItem(cmd, pass, *item, projView);
    }

    // --- SKYBOX (Step 14) ----------------------------------------------------------
    // Drawn AFTER the opaque geometry (which filled the depth buffer) so its LEQUAL
    // test + far-plane depth make it fill ONLY the background pixels. It must also come
    // BEFORE the transparent pass: the sky writes no depth, so if transparent objects
    // (which also write no depth) drew first, the sky would paint over them. Resolving
    // the background first lets translucent surfaces blend correctly over the sky.
    if (skyboxCubemap_ != nullptr && skyboxEnabled_) {
        recordSkybox(cmd, pass, view, projection);
    }

    // --- TRANSPARENT PASS: blending ON, depth-write OFF ----------------------------
    // Draw the sorted (far → near) translucent items so each composites over what's
    // already there. Binding the transparent pipeline reset the pass's sampler bindings,
    // so re-establish the per-frame shadow/IBL maps (and re-push the light uniform).
    if (!transparentItems_.empty()) {
        SDL_BindGPUGraphicsPipeline(pass, transparentPipeline_);
        bindFrameLighting(cmd, pass, cameraPos, lights, lightViewProj);
        for (const RenderItem* item : transparentItems_) {
            submitItem(cmd, pass, *item, projView);
        }
    }

    // --- DEBUG OVERLAY (Step 22) ---------------------------------------------------
    // Drawn LAST, over the finished scene, using the same camera projView. A no-op
    // unless the app queued debug lines this frame (they were uploaded up front in
    // renderSceneAndPost). Depth-tested but not depth-writing, so lines are occluded
    // by geometry yet never disturb it.
    recordDebug(cmd, pass, projView);
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

void GpuRenderer::uploadDebugLines(std::span<const DebugVertex> lines) {
    // IMMEDIATE MODE: every frame's debug geometry is brand new, so rather than
    // synchronize writes to a persistent buffer (which the previous frame's draw
    // might still be reading), we simply release last frame's buffer and build a
    // fresh one. SDL_ReleaseGPUBuffer defers the actual free until the GPU is done
    // with it, so this is safe even while the previous frame is still in flight —
    // and no two frames ever touch the same buffer, so there's nothing to fence.
    if (debugVertexBuffer_ != nullptr) {
        SDL_ReleaseGPUBuffer(device_, debugVertexBuffer_);
        debugVertexBuffer_ = nullptr;
    }
    debugVertexCount_ = 0;

    if (lines.empty()) {
        return;  // nothing queued this frame — recordDebug will draw nothing
    }

    // Reuse the shared staging-upload helper (the same one behind every mesh).
    const Uint32 bytes = static_cast<Uint32>(lines.size_bytes());
    debugVertexBuffer_ = uploadToGpuBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, lines.data(), bytes);
    if (debugVertexBuffer_ != nullptr) {
        debugVertexCount_ = static_cast<Uint32>(lines.size());
    }
}

void GpuRenderer::recordDebug(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                              const Mat4& viewProj) const {
    if (debugVertexCount_ == 0 || debugLinePipeline_ == nullptr) {
        return;  // no lines this frame (the common case — debug draw is opt-in)
    }

    // Binding a new pipeline resets the pass's bindings, so set everything the
    // debug draw needs: the pipeline, the view-projection uniform, the vertex
    // buffer, then one line-list draw of all the collected vertices.
    SDL_BindGPUGraphicsPipeline(pass, debugLinePipeline_);
    SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, viewProj.m, sizeof(viewProj.m));

    SDL_GPUBufferBinding vb = {};
    vb.buffer = debugVertexBuffer_;
    SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vb, /*num_bindings=*/1);
    SDL_DrawGPUPrimitives(pass, debugVertexCount_, /*num_instances=*/1,
                          /*first_vertex=*/0, /*first_instance=*/0);
}

void GpuRenderer::submitItem(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                             const RenderItem& item, const Mat4& projView) const {
    // buildRenderQueue only ever enqueues drawables, so both are guaranteed non-null
    // here — the mesh/material presence check that recordNode used to do now lives in
    // the traversal (RenderQueue.cpp), leaving this "submit" half to just bind + draw.
    const Mesh*     mesh     = item.mesh;
    const Material* material = item.material;
    {
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

        // The EMISSIVE map (Step 16) sits at slot 8 — slots 5–7 are the shared IBL maps,
        // bound once per frame — so it's bound on its own rather than extending maps[].
        // No emissive map ⇒ the white fallback, which (with a zero factor) adds nothing.
        const SDL_GPUTextureSamplerBinding emissiveBinding = {
            material->emissive ? material->emissive->handle() : whiteTex_, sampler_ };
        SDL_BindGPUFragmentSamplers(pass, /*first_slot=*/8, &emissiveBinding, /*num_bindings=*/1);

        // Push this material's parameters (fragment set=3, binding 1), matching the two
        // vec4s of MaterialUBO in triangle.frag: {metallic, roughness, -, opacity} then
        // the emissive factor {r, g, b, -}. Per draw, so each object's PBR + glow + alpha
        // is its own. `opacity` lands in material.w — the shader multiplies it by the
        // albedo map's alpha to form outColor.a (only the transparent pipeline blends it).
        const float materialParams[8] = {
            material->metallic, material->roughness, 0.0f, material->opacity,
            material->emissiveFactor[0], material->emissiveFactor[1],
            material->emissiveFactor[2], 0.0f };
        SDL_PushGPUFragmentUniformData(cmd, /*slot=*/1, materialParams, sizeof(materialParams));

        // Bind THIS mesh's geometry. Different meshes (e.g. cube vs. ground plane)
        // live in different buffers, so we (re)bind per drawn node.
        SDL_GPUBufferBinding vertexBinding = {};
        vertexBinding.buffer = mesh->vertexBuffer();
        SDL_BindGPUVertexBuffers(pass, /*first_slot=*/0, &vertexBinding, /*num_bindings=*/1);
        SDL_GPUBufferBinding indexBinding = {};
        indexBinding.buffer = mesh->indexBuffer();
        SDL_BindGPUIndexBuffer(pass, &indexBinding, mesh->indexElementSize());

        // Push the three matrices the vertex shader needs: the full MVP (proj·view·
        // world, to clip space), the model/world matrix on its own (so the fragment
        // shader can light in world space), and the NORMAL MATRIX. `model` is the
        // item's world matrix, cached by updateWorldTransforms — world already folds
        // in every ancestor's transform, so parented objects move as a group for free.
        //
        // normalMatrix = transpose(inverse(model)) is the transform that keeps
        // surface normals perpendicular under a non-uniform scale (Step 19). The
        // shader takes its mat3() — for an affine matrix the translation part lands
        // outside that 3x3 block, so this is exactly the classic 3x3 normal matrix.
        struct VertexUniform { Mat4 mvp; Mat4 model; Mat4 normalMatrix; };
        const Mat4 model = item.world;
        const VertexUniform u = { projView * model, model, transpose(inverse(model)) };
        SDL_PushGPUVertexUniformData(cmd, /*slot=*/0, &u, sizeof(u));
        SDL_DrawGPUIndexedPrimitives(pass, mesh->indexCount(), /*num_instances=*/1,
                                     /*first_index=*/0, /*vertex_offset=*/0,
                                     /*first_instance=*/0);
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
        // Image-based lighting resources (Step 15). SDL_ReleaseGPU* tolerate null.
        SDL_ReleaseGPUTexture(device_, iblIrradiance_);
        SDL_ReleaseGPUTexture(device_, iblPrefilter_);
        SDL_ReleaseGPUTexture(device_, iblBrdfLut_);
        iblIrradiance_ = iblPrefilter_ = iblBrdfLut_ = nullptr;
        if (iblSampler_ != nullptr) {
            SDL_ReleaseGPUSampler(device_, iblSampler_);
            iblSampler_ = nullptr;
        }
        if (iblIrradiancePipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, iblIrradiancePipeline_);
            iblIrradiancePipeline_ = nullptr;
        }
        if (iblPrefilterPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, iblPrefilterPipeline_);
            iblPrefilterPipeline_ = nullptr;
        }
        if (iblBrdfPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, iblBrdfPipeline_);
            iblBrdfPipeline_ = nullptr;
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
        if (transparentPipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, transparentPipeline_);
            transparentPipeline_ = nullptr;
        }
        // Debug draw (Step 22): the line pipeline and this frame's transient vertex
        // buffer (null unless a frame with debug lines ran). Release tolerates null.
        if (debugLinePipeline_ != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(device_, debugLinePipeline_);
            debugLinePipeline_ = nullptr;
        }
        if (debugVertexBuffer_ != nullptr) {
            SDL_ReleaseGPUBuffer(device_, debugVertexBuffer_);
            debugVertexBuffer_ = nullptr;
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

void GpuRenderer::renderFrame(const FrameView& fv) {
    // Unpack the frame bundle into the locals the body below already speaks in.
    // (`fv.root` is guaranteed non-null by the caller — the engine's loop.)
    const SDL_FColor& clearColor         = fv.clearColor;
    const Mat4& view                     = fv.view;
    const Node& sceneRoot                = *fv.root;
    const Vec3& cameraPos                = fv.cameraPos;
    const std::span<const Light> lights  = fv.lights;
    const PostSettings& post             = fv.post;
    const std::span<const DebugVertex> debugLines = fv.debugLines;

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
                           cameraPos, lights, clearColor, post, debugLines);
    }

    // ------------------------------------------------------------------------
    //  4. Submit.
    //     Hand the recorded command buffer to the GPU for execution and queue
    //     the result for display. We do NOT block waiting for it to finish —
    //     the CPU is free to start building the next frame immediately.
    // ------------------------------------------------------------------------
    SDL_SubmitGPUCommandBuffer(cmd);
}

bool GpuRenderer::captureFrame(const char* path, const FrameView& fv) {
    // Unpack the frame bundle into the locals the body below already speaks in
    // (mirrors renderFrame). `fv.root` is guaranteed non-null by the caller.
    const SDL_FColor& clearColor         = fv.clearColor;
    const Mat4& view                     = fv.view;
    const Node& sceneRoot                = *fv.root;
    const Vec3& cameraPos                = fv.cameraPos;
    const std::span<const Light> lights  = fv.lights;
    const PostSettings& post             = fv.post;
    const std::span<const DebugVertex> debugLines = fv.debugLines;

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
                       lights, clearColor, post, debugLines);

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
