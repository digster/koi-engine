// ============================================================================
//  GpuRenderer.hpp — owns the SDL3 GPU device and renders a frame
// ----------------------------------------------------------------------------
//  This is our window into the graphics card. It owns the GPU device, the
//  swapchain, the graphics pipeline, and the per-frame depth buffer — the
//  plumbing every later feature builds on (device -> swapchain -> command
//  buffer -> render pass -> submit).
//
//  As of Step 5 the renderer no longer owns the geometry. Instead it is a
//  FACTORY for Mesh objects (createMesh) and a CONSUMER of a scene graph: given
//  a scene root, it walks the tree and draws each node's mesh at that node's
//  world transform. This decouples "how to talk to the GPU" (here) from "what's
//  in the world" (scene/Node).
//
//  See documentation/docs/01-window-and-render-loop.html for the GPU concepts named below and
//  documentation/docs/06-meshes-and-scene-graph.html for meshes + the scene graph.
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

#include <memory>  // std::shared_ptr — meshes are shared, owned via shared_ptr
#include <span>    // std::span — a (pointer, length) view over caller-owned geometry

#include "math/Mat4.hpp"
#include "renderer/PostProcess.hpp"  // PostSettings (passed into renderFrame/captureFrame)
#include "renderer/Vertex.hpp"  // createMesh takes a std::span<const Vertex>
#include "scene/Light.hpp"  // std::span<const Light> passed into renderFrame/captureFrame (SDL-free)

namespace koi {

class Mesh;     // manufactured by createMesh; full definition in renderer/Mesh.hpp
class Node;     // the scene-graph root we traverse to draw; scene/Node.hpp
class Texture;  // manufactured by loadTexture; full definition in renderer/Texture.hpp

// Map a GPU color-target format to the equivalent SDL surface pixel format, so
// downloaded pixels can be wrapped in an SDL_Surface and saved. Pure function
// (no GPU/IO) → unit-testable. Returns SDL_PIXELFORMAT_UNKNOWN for formats we
// don't handle. Used by GpuRenderer::captureFrame.
[[nodiscard]] SDL_PixelFormat gpuColorFormatToPixelFormat(SDL_GPUTextureFormat format);

class GpuRenderer {
public:
    // Construction creates a GPU "device" (our handle to the graphics card),
    // binds it to the given window, and builds the graphics pipeline. On failure,
    // isValid() returns false.
    explicit GpuRenderer(SDL_Window* window);

    // Destruction unbinds the window and destroys the device. SDL waits for the
    // GPU to finish any in-flight work before tearing down, so this is safe.
    // IMPORTANT: any Mesh made by createMesh frees its buffers through this
    // device, so all meshes must be released BEFORE this renderer (the Engine
    // tears the scene down first — see Engine::shutdown()).
    ~GpuRenderer();

    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;

    // Valid once the device, the pipeline, AND the shared sampler are ready, so the
    // Engine aborts with a clear message if (e.g.) the compiled shaders are missing.
    // The renderer owns no geometry or textures — those are created separately via
    // createMesh / loadTexture.
    [[nodiscard]] bool isValid() const {
        return device_ != nullptr && trianglePipeline_ != nullptr && sampler_ != nullptr &&
               shadowPipeline_ != nullptr && shadowMap_ != nullptr && shadowSampler_ != nullptr &&
               postSampler_ != nullptr && brightPipeline_ != nullptr &&
               blurPipeline_ != nullptr && compositePipeline_ != nullptr &&
               fxaaPipeline_ != nullptr;
    }

    // Upload geometry into a new Mesh and return it (shared, so many scene nodes
    // can reference one Mesh). `vertices`/`indices` are copied into GPU buffers via
    // the staging-upload path. Two index widths: 16-bit (Uint16) for hand-built
    // primitives, 32-bit (Uint32) for loaded models that exceed 65 535 vertices.
    // Returns nullptr (after logging) on failure. This is the ONLY place meshes are
    // born, because it's the only thing holding both the device and the upload helper.
    [[nodiscard]] std::shared_ptr<Mesh> createMesh(std::span<const Vertex> vertices,
                                                   std::span<const Uint16> indices);
    [[nodiscard]] std::shared_ptr<Mesh> createMesh(std::span<const Vertex> vertices,
                                                   std::span<const Uint32> indices);

    // Load an image file (BMP) from `path` and upload it into a new GPU Texture
    // (shared, so it could be reused by many draws). Returns nullptr (after logging)
    // on failure. Like createMesh, this is the only place textures are born — it
    // holds both the device and the upload helper.
    [[nodiscard]] std::shared_ptr<Texture> loadTexture(const char* path);

    // Render exactly one frame: acquire a swapchain image, draw every mesh in
    // `sceneRoot` (each through its own world matrix and its node's material) as seen
    // through the camera `view` into an off-screen HDR target, run the post-processing
    // chain (`post` selects which effects), and present the result. `cameraPos` (the
    // eye in world space) feeds the lighting's specular highlight. `lights` is the
    // scene's active light list (Step 11); light 0 is the shadow-casting sun.
    void renderFrame(const SDL_FColor& clearColor, const Mat4& view,
                     const Node& sceneRoot, const Vec3& cameraPos,
                     std::span<const Light> lights, const PostSettings& post);

    // Render one frame into an OFF-SCREEN texture (not the window), download the
    // pixels back to the CPU, and save them to `path` as a BMP. Our headless
    // visual-debugging tool: it captures exactly what the engine draws (the same
    // `sceneRoot` + `texture` through `view`) without a visible window. Returns
    // false (after logging) on failure. See docs / CLAUDE.md.
    [[nodiscard]] bool captureFrame(const char* path, const SDL_FColor& clearColor,
                                    const Mat4& view, const Node& sceneRoot,
                                    const Vec3& cameraPos, std::span<const Light> lights,
                                    const PostSettings& post);

private:
    // Build the graphics pipeline (loads + compiles shaders, and describes the
    // vertex input layout, into an immutable pipeline object). Called once from
    // the constructor.
    bool createTrianglePipeline();

    // Create one GPU buffer of `usage` (VERTEX or INDEX), upload `size` bytes
    // from `data` into it via a staging transfer buffer + copy pass, and return
    // it. Returns nullptr (after logging) on failure. The shared helper behind
    // every mesh upload (see createMesh).
    SDL_GPUBuffer* uploadToGpuBuffer(SDL_GPUBufferUsageFlags usage,
                                     const void* data, Uint32 size);

    // Shared body of the two createMesh overloads: upload the vertices + raw index
    // bytes and wrap them in a Mesh tagged with `elemSize` (16- or 32-bit).
    std::shared_ptr<Mesh> createMeshImpl(std::span<const Vertex> vertices,
                                         const void* indexData, Uint32 indexBytes,
                                         Uint32 indexCount,
                                         SDL_GPUIndexElementSize elemSize);

    // Create a 2D sampled texture (R8G8B8A8_UNORM) of (width, height) and upload
    // `pixels` (tightly packed RGBA, 4 bytes/texel) into it via a staging transfer
    // buffer + copy pass — the 2D mirror of uploadToGpuBuffer. Returns nullptr
    // (after logging) on failure. The helper behind loadTexture.
    //
    // When `withMips` is true the texture is given a full mip chain (num_levels from
    // its size) and the extra COLOR_TARGET usage the GPU needs to generate them, and
    // SDL_GenerateMipmapsForGPUTexture is run after the upload — so minified/oblique
    // textures don't shimmer (Step 13). Tiny 1×1 fallbacks pass false.
    SDL_GPUTexture* uploadToGpuTexture(const void* pixels, Uint32 width, Uint32 height,
                                       bool withMips = true);

    // Create a solid-colour 1×1 RGBA texture (no mips) — used for the neutral map
    // fallbacks a material binds when it lacks a given map. See whiteTex_/flatNormalTex_.
    SDL_GPUTexture* createSolidTexture(Uint8 r, Uint8 g, Uint8 b, Uint8 a);

    // Create the one sampler all textures are read through (linear filtering, mipmap
    // + anisotropic filtering, REPEAT wrap so tiled UVs work). Called once from the ctor.
    bool createSampler();

    // Create the neutral 1×1 fallback maps (whiteTex_, flatNormalTex_) a material binds
    // when it lacks a metallic-roughness / AO / normal map. Called once from the ctor.
    bool createFallbackTextures();

    // Create the shadow-mapping resources (Step 9): a depth texture that is both a
    // render target and sampleable (the "shadow map"), a CLAMP sampler to read it,
    // and a depth-only pipeline (shadow.vert/.frag). Called once from the ctor,
    // after the main pipeline (it reuses the chosen depth format).
    bool createShadowResources();

    // PASS 1 of shadow mapping: render the scene's depth from the light's point of
    // view into the shadow map. Begins its own depth-only render pass, walks the
    // tree pushing lightViewProj·world per node, ends. Done before the main pass.
    void renderShadowPass(SDL_GPUCommandBuffer* cmd, const Node& root,
                          const Mat4& lightViewProj) const;

    // Recursive worker for the shadow pass: draw `node`'s mesh (depth only) under
    // `lightViewProj`, then recurse. (No material/texture needed — only depth.)
    void recordShadowNode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                          const Node& node, const Mat4& lightViewProj) const;

    // Record the whole scene into an already-begun (main) render pass: bind the
    // pipeline, bind the shadow map, pack `lights` + `cameraPos` + `lightViewProj`
    // into the per-frame light uniform, build the projection from `aspect`, then walk
    // `root` drawing each node — binding its material's texture + pushing its
    // {mvp, model} and material uniforms. Shared by the live (renderFrame) and
    // off-screen (captureFrame) paths so they can't drift.
    void recordScene(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                     const Node& root, const Mat4& view, float aspect,
                     const Vec3& cameraPos, std::span<const Light> lights,
                     const Mat4& lightViewProj) const;

    // Recursive worker for recordScene: draw `node`'s mesh (if any) using the
    // already-combined `projView` matrix, then recurse into its children. Reads
    // the node's cached world matrix (filled by Node::updateWorldTransforms).
    void recordNode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                    const Node& node, const Mat4& projView) const;

    // Ensure the depth texture exists and matches (w, h), recreating it on resize.
    // Returns false (after logging) if creation fails. Called each frame before
    // the render pass, since the window — and thus the needed depth size — can
    // change. See documentation/docs/04 for what a depth buffer is and why we need one.
    bool ensureDepthTexture(Uint32 width, Uint32 height);

    // ----- Post-processing (Step 10) ---------------------------------------
    // Create the shared linear-clamp sampler and the four fullscreen pipelines
    // (bloom bright-pass, blur, tone-map composite, FXAA). Called once from the
    // constructor, after the main + shadow pipelines.
    bool createPostResources();

    // Build one fullscreen-pass pipeline: `vs` is the shared fullscreen.vert; `frag`
    // is the effect's fragment shader; `targetFormat` is the color format it writes
    // (HDR for bloom passes, swapchain format for composite/FXAA); `numSamplers` is
    // how many textures the fragment shader reads. No vertex input, no depth.
    SDL_GPUGraphicsPipeline* buildFullscreenPipeline(SDL_GPUShader* vs, const char* frag,
                                                     SDL_GPUTextureFormat targetFormat,
                                                     Uint32 numSamplers);

    // Create one sampleable color-target texture (COLOR_TARGET | SAMPLER) of (w, h).
    SDL_GPUTexture* createColorTarget(Uint32 width, Uint32 height,
                                      SDL_GPUTextureFormat format);

    // Ensure the off-screen render targets exist at (w, h): the HDR scene target, the
    // depth buffer, the half-res bloom ping-pong pair, and the LDR target — recreating
    // them all on resize. Returns false (after logging) on failure.
    bool ensureSceneTargets(Uint32 width, Uint32 height);

    // The whole live/offscreen render: shadow pass → scene into the HDR target →
    // post-processing chain, writing the final image into `finalColor` (the swapchain
    // image for renderFrame, or the capture target for captureFrame). Shared so the
    // two paths can't drift, exactly like recordScene.
    void renderSceneAndPost(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* finalColor,
                            Uint32 width, Uint32 height, const Mat4& view,
                            const Node& sceneRoot, const Vec3& cameraPos,
                            std::span<const Light> lights,
                            const SDL_FColor& clearColor, const PostSettings& post);

    // Run the fullscreen post-processing passes over the already-rendered HDR scene,
    // ending with the final image written into `finalColor`. `width`/`height` are the
    // final target's size (for FXAA's 1/resolution). Const: issues GPU work but mutates
    // no members.
    void runPostChain(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* finalColor,
                      Uint32 width, Uint32 height, const PostSettings& post) const;

    // Small shared helpers for the fullscreen passes.
    SDL_GPURenderPass* beginColorPass(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target,
                                      SDL_GPULoadOp loadOp, bool cycle) const;
    void bindPostSampler(SDL_GPURenderPass* pass, Uint32 slot, SDL_GPUTexture* tex) const;
    static void drawFullscreen(SDL_GPURenderPass* pass);

    SDL_Window*    window_ = nullptr;  // not owned — the Engine owns the Window
    SDL_GPUDevice* device_ = nullptr;  // owned — released in the destructor

    // The graphics pipeline that draws our meshes: it bundles the vertex +
    // fragment shaders together with fixed-function state (primitive type, the
    // color target's format, the vertex input layout, etc.) into one object the
    // GPU can switch to quickly. All meshes share this one pipeline; only the
    // bound buffers and the MVP uniform change per draw.
    SDL_GPUGraphicsPipeline* trianglePipeline_ = nullptr;  // owned

    // The one sampler every texture is read through: it describes HOW to sample
    // (linear filtering between texels, REPEAT addressing past the [0,1] edges so
    // tiled UVs repeat). Reusable device state, independent of any particular
    // texture, so the renderer owns a single shared one. Created in the constructor.
    SDL_GPUSampler* sampler_ = nullptr;  // owned

    // Neutral 1×1 fallback maps (Step 13), bound when a material omits a given map.
    // whiteTex_ (255,255,255) makes `factor × sampledChannel` reduce to the scalar
    // factor (metallic-roughness) and AO reduce to 1; flatNormalTex_ (128,128,255)
    // decodes to tangent-space (0,0,1), i.e. no normal perturbation. Together they let
    // map-less materials render exactly as they did in Step 12, with no shader branch.
    SDL_GPUTexture* whiteTex_      = nullptr;  // owned
    SDL_GPUTexture* flatNormalTex_ = nullptr;  // owned

    // The depth buffer: a texture the same size as the color target that stores,
    // per pixel, the depth of the nearest fragment drawn so far. With the depth
    // test enabled, the GPU keeps a new fragment only if it's closer — that's how
    // near faces correctly hide far ones, regardless of draw order. Created lazily
    // and resized to match the swapchain.
    SDL_GPUTexture*      depthTexture_ = nullptr;  // owned
    Uint32              depthWidth_   = 0;
    Uint32              depthHeight_  = 0;
    SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;  // chosen at pipeline build

    // Shadow mapping (Step 9). The shadow map is a fixed-size depth texture we
    // render the scene's depth INTO from the light's view (pass 1) and SAMPLE in
    // the main pass (pass 2) to decide what's occluded. The shadow pipeline is a
    // depth-only pipeline (no color target); the shadow sampler reads the map with
    // CLAMP addressing so fragments outside the light's box read as lit.
    SDL_GPUGraphicsPipeline* shadowPipeline_ = nullptr;  // owned
    SDL_GPUTexture*          shadowMap_      = nullptr;  // owned
    SDL_GPUSampler*          shadowSampler_  = nullptr;  // owned

    // Post-processing (Step 10). The scene is rendered into an off-screen HDR color
    // target (sceneHdr_) instead of straight to the swapchain; a chain of fullscreen
    // passes then samples it. Bloom runs at half-res through a ping-pong pair
    // (bloomA_/bloomB_); the tone-mapped result lands in ldr_; FXAA writes the final
    // image to the swapchain. All are lazily (re)created to match the window size.
    SDL_GPUGraphicsPipeline* brightPipeline_    = nullptr;  // owned (bloom bright-pass)
    SDL_GPUGraphicsPipeline* blurPipeline_      = nullptr;  // owned (separable Gaussian)
    SDL_GPUGraphicsPipeline* compositePipeline_ = nullptr;  // owned (combine + tone-map)
    SDL_GPUGraphicsPipeline* fxaaPipeline_      = nullptr;  // owned (anti-aliasing)
    SDL_GPUSampler*          postSampler_       = nullptr;  // owned (linear, clamp)

    SDL_GPUTexture* sceneHdr_ = nullptr;  // owned — full-res HDR scene target
    SDL_GPUTexture* ldr_      = nullptr;  // owned — full-res tone-mapped (pre-FXAA) target
    SDL_GPUTexture* bloomA_   = nullptr;  // owned — half-res bloom ping-pong
    SDL_GPUTexture* bloomB_   = nullptr;  // owned — half-res bloom ping-pong
    Uint32 postWidth_   = 0;  // full-res size sceneHdr_/ldr_ were last built at
    Uint32 postHeight_  = 0;
    Uint32 bloomWidth_  = 0;  // half-res size of the bloom targets (for the blur step)
    Uint32 bloomHeight_ = 0;
};

}  // namespace koi
