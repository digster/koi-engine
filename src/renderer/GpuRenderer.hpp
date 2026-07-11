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
//  See docs/tuts/01-window-and-render-loop.html for the GPU concepts named below and
//  docs/tuts/06-meshes-and-scene-graph.html for meshes + the scene graph.
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

#include <array>   // std::array — the six cubemap face paths
#include <memory>  // std::shared_ptr — meshes are shared, owned via shared_ptr
#include <span>    // std::span — a (pointer, length) view over caller-owned geometry
#include <string>  // std::string — cubemap face paths
#include <vector>  // std::vector — the render queue (Step 20)

#include "math/Mat4.hpp"
#include "renderer/FrameView.hpp"    // FrameView — the one bundle renderFrame/captureFrame consume
#include "renderer/PostProcess.hpp"  // PostSettings (a FrameView field)
#include "renderer/RenderQueue.hpp"  // RenderItem — the flat draw list we build + cull (Step 20)
#include "renderer/Vertex.hpp"  // createMesh takes a std::span<const Vertex>
#include "scene/Light.hpp"  // Light — a FrameView field (SDL-free)

namespace koi {

class Mesh;     // manufactured by createMesh; full definition in renderer/Mesh.hpp
class Node;     // the scene-graph root we traverse to draw; scene/Node.hpp
class Texture;  // manufactured by loadTexture; full definition in renderer/Texture.hpp

// Map a GPU color-target format to the equivalent SDL surface pixel format, so
// downloaded pixels can be wrapped in an SDL_Surface and saved. Pure function
// (no GPU/IO) → unit-testable. Returns SDL_PIXELFORMAT_UNKNOWN for formats we
// don't handle. Used by GpuRenderer::captureFrame.
[[nodiscard]] SDL_PixelFormat gpuColorFormatToPixelFormat(SDL_GPUTextureFormat format);

// Per-instance data for instanced draws (Step 24): the transforms that used to be
// pushed as a per-draw uniform, now packed one-per-copy into the color pass's
// instance vertex buffer. The field layout is a binding contract with triangle.vert's
// instance attributes — `model` feeds locations 5–8, `normalMatrix` feeds 9–12. Mat4
// is a column-major float[16] (64 bytes), so this packs to 128 bytes with no padding.
// (The shadow pass, being depth-only, uses a bare Mat4 model array instead.)
struct InstanceData {
    Mat4 model;         // this copy's world matrix
    Mat4 normalMatrix;  // transpose(inverse(model)) — keeps normals perpendicular
};

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
        return device_ != nullptr && trianglePipeline_ != nullptr &&
               transparentPipeline_ != nullptr && sampler_ != nullptr &&
               shadowPipeline_ != nullptr && shadowMap_ != nullptr && shadowSampler_ != nullptr &&
               postSampler_ != nullptr && brightPipeline_ != nullptr &&
               blurPipeline_ != nullptr && compositePipeline_ != nullptr &&
               fxaaPipeline_ != nullptr && skyboxPipeline_ != nullptr &&
               skyboxSampler_ != nullptr && skyboxMesh_ != nullptr &&
               iblIrradiancePipeline_ != nullptr && iblPrefilterPipeline_ != nullptr &&
               iblBrdfPipeline_ != nullptr && iblIrradiance_ != nullptr &&
               iblPrefilter_ != nullptr && iblBrdfLut_ != nullptr && iblSampler_ != nullptr &&
               debugLinePipeline_ != nullptr &&
               hudPipeline_ != nullptr && hudAtlas_ != nullptr && hudSampler_ != nullptr;
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

    // Load an image file from `path` and upload it into a new GPU Texture (shared, so
    // it could be reused by many draws). Returns nullptr (after logging) on failure.
    // Like createMesh, this is the only place textures are born — it holds both the
    // device and the upload helper.
    //
    // BMP is decoded via SDL (no extra dependency); every other format (PNG/JPG, used
    // by glTF — Step 16) via stb_image. Pass `srgb = true` for COLOUR images
    // (base-colour, emissive) so the GPU decodes their gamma to linear before shading;
    // leave it false for DATA maps (metallic-roughness, normal, AO) whose bytes are
    // already linear (see createTextureFromRGBA / uploadToGpuTexture).
    [[nodiscard]] std::shared_ptr<Texture> loadTexture(const char* path, bool srgb = false);

    // Upload already-decoded, tightly-packed RGBA pixels (4 bytes/texel) into a new
    // GPU Texture. This is the entry point the glTF loader uses after decoding an
    // embedded image with stb_image — the shared tail of loadTexture, exposed so image
    // bytes that never touched the filesystem can still become a Texture. `srgb` picks
    // an sRGB texture format for colour maps; see uploadToGpuTexture. `withMips` mirrors
    // that helper's flag — decoded images want a mip chain, but 1×1 solids pass false.
    [[nodiscard]] std::shared_ptr<Texture> createTextureFromRGBA(const void* pixels,
                                                                 Uint32 width, Uint32 height,
                                                                 bool srgb = false,
                                                                 bool withMips = true);

    // Load the six BMP faces of an environment CUBEMAP (Step 14) and upload them
    // into a single cube GPU texture the renderer owns, then draw it as the sky
    // behind the scene. `facePaths` is in SDL's cube layer order: +X, -X, +Y, -Y,
    // +Z, -Z. All faces must share one square size. Returns false (after logging)
    // on failure. Unlike loadTexture (which hands the caller a Texture), the sky is
    // engine-global infrastructure — like the shadow map — so the renderer holds it.
    bool loadCubemap(const std::array<std::string, 6>& facePaths);

    // Show/hide the loaded skybox at runtime (the demo binds this to a key). No-op
    // if no cubemap has been loaded. The sky always draws when enabled AND present.
    void setSkyboxEnabled(bool enabled) { skyboxEnabled_ = enabled; }
    [[nodiscard]] bool skyboxEnabled() const { return skyboxEnabled_; }

    // Enable/disable image-based lighting at runtime (Step 15; the demo binds this to a
    // key). When off, the ambient term falls back to the flat constant fill (the Step 12
    // look) — a clean A/B for seeing exactly what the environment contributes. Has no
    // visible effect until a cubemap has been loaded and baked (loadCubemap → bakeIbl).
    void setIblEnabled(bool enabled) { iblEnabled_ = enabled; }
    [[nodiscard]] bool iblEnabled() const { return iblEnabled_; }

    // Enable/disable view-frustum culling at runtime (Step 20; the demo binds this to a
    // key). When on, the main pass skips draw items whose world bounds fall entirely
    // outside the camera frustum — invisible work the GPU would otherwise do. On by
    // default; a clean A/B for the drawn/culled counts the renderer logs. The SHADOW
    // pass is never camera-culled (off-screen casters still cast in-view shadows).
    void setFrustumCullingEnabled(bool enabled) { frustumCullingEnabled_ = enabled; }
    [[nodiscard]] bool frustumCullingEnabled() const { return frustumCullingEnabled_; }

    // Enable/disable back-to-front sorting of transparent objects at runtime (Step 21;
    // the demo binds this to a key). On by default. With it OFF, translucent objects
    // draw in queue order instead of far-to-near, so overlapping ones composite in the
    // wrong order — a deliberate A/B for SEEING why the painter's-algorithm sort exists.
    // No effect on opaque objects (the depth buffer orders those regardless).
    void setTransparentSortEnabled(bool enabled) { transparentSortEnabled_ = enabled; }
    [[nodiscard]] bool transparentSortEnabled() const { return transparentSortEnabled_; }

    // The camera view-projection (projection · view) used by the LAST rendered
    // frame's main pass — the exact matrix Step 20's culler built its frustum from.
    // Exposed (Step 22) so the app can "freeze" it and hand it back to DebugDraw::
    // frustum, drawing a wireframe of the volume that was culled against. Identity
    // until the first frame has been recorded.
    [[nodiscard]] Mat4 lastCameraViewProjection() const { return lastCameraViewProj_; }

    // Per-frame draw-call accounting (Step 24). `items` is how many drawables the
    // main color pass submitted this frame (after culling); `drawCalls` is how many
    // actual GPU draws that took — fewer once instancing collapses identical runs.
    // The gap between them is the batching win, which the demo HUD displays.
    struct DrawStats {
        Uint32 items     = 0;
        Uint32 drawCalls = 0;
    };
    [[nodiscard]] DrawStats lastDrawStats() const { return lastDrawStats_; }

    // Render exactly one frame from a FrameView `fv`: acquire a swapchain image,
    // draw every mesh in `fv.root` (each through its own world matrix and its node's
    // material) as seen through the camera `fv.view` into an off-screen HDR target,
    // run the post-processing chain (`fv.post` selects which effects), and present the
    // result. `fv.cameraPos` (the eye in world space) feeds the specular highlight;
    // `fv.lights` is the active light list (Step 11), light 0 the shadow-casting sun.
    void renderFrame(const FrameView& fv);

    // Render one frame into an OFF-SCREEN texture (not the window), download the
    // pixels back to the CPU, and save them to `path` as a BMP. Our headless
    // visual-debugging tool: it captures exactly what the engine draws (the same
    // `fv` the live path would) without a visible window. Returns false (after
    // logging) on failure. See docs / CLAUDE.md.
    [[nodiscard]] bool captureFrame(const char* path, const FrameView& fv);

private:
    // Build the graphics pipeline (loads + compiles shaders, and describes the
    // vertex input layout, into an immutable pipeline object). Called once from
    // the constructor.
    bool createTrianglePipeline();

    // Build the debug-line pipeline (Step 22): the minimal unlit debug_line
    // shaders, a 2-attribute vertex layout (position + colour) at DebugVertex
    // stride, and LINE-LIST topology (every 2 vertices = one segment). Depth TEST
    // stays on (so lines are occluded by geometry) but depth WRITE is off (debug
    // overlays never disturb the scene's depth). Called once from the constructor.
    bool createDebugPipeline();

    // Upload this frame's debug line vertices into a GPU vertex buffer for the
    // draw in recordDebug (Step 22). A fresh buffer is built each frame (the old
    // one is released — SDL defers the free until the GPU is done, so no two frames
    // ever share a buffer and there's nothing to synchronize). Records the vertex
    // count; an empty list uploads nothing and the overlay draw is skipped.
    void uploadDebugLines(std::span<const DebugVertex> lines);

    // Draw the uploaded debug lines into the already-begun main render pass
    // (Step 22): bind the debug pipeline, push `viewProj`, and issue one line-list
    // draw of debugVertexCount_ vertices. A no-op when nothing was queued. Called
    // at the END of recordScene so lines overlay the finished scene.
    void recordDebug(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                     const Mat4& viewProj) const;

    // Build the HUD overlay resources (Step 23): bake the embedded 8x8 bitmap font
    // (Font.hpp) into an RGBA atlas texture, create a NEAREST-filter clamp sampler
    // (crisp, un-blurred glyphs), and build the textured overlay pipeline — a
    // triangle list with alpha blending, NO depth, targeting the swapchain (LDR)
    // format so the HUD draws onto the final post-processed image. Called once from
    // the constructor, after the debug pipeline.
    bool createHudResources();

    // Upload this frame's HUD vertices into a GPU vertex buffer for the draw in
    // recordHud (Step 23). Like uploadDebugLines, a fresh buffer is built each frame
    // (immediate mode — no state between frames); an empty list uploads nothing and
    // the overlay pass is skipped.
    void uploadHud(std::span<const HudVertex> verts);

    // Draw the uploaded HUD into an already-begun overlay pass on the FINAL image
    // (Step 23): bind the HUD pipeline + atlas, push the viewport size (pixels→NDC),
    // and issue one triangle-list draw. A no-op when nothing was queued. Called after
    // the post chain, so the HUD sits on top of the tone-mapped, anti-aliased frame.
    void recordHud(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                   Uint32 width, Uint32 height) const;

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
    //
    // `srgb` (Step 16) selects an sRGB texture format so the GPU converts the stored
    // gamma-encoded bytes to LINEAR when sampled (correct for colour/base-colour maps).
    // The bytes uploaded are identical either way — sRGB is purely a sample-time decode.
    SDL_GPUTexture* uploadToGpuTexture(const void* pixels, Uint32 width, Uint32 height,
                                       bool withMips = true, bool srgb = false);

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

    // Create the skybox resources (Step 14): the depth-aware skybox pipeline
    // (skybox.vert/.frag), a CLAMP_TO_EDGE cubemap sampler, and the shared cube mesh
    // drawn around the camera. Called once from the ctor, after the main pipeline
    // (it reuses the chosen depth format + kSceneHdrFormat). The cubemap TEXTURE
    // itself is loaded later via loadCubemap.
    bool createSkyboxResources();

    // Upload six equal-size RGBA face images into one cube GPU texture (type CUBE,
    // 6 layers), generating a mip chain. `faces` is in +X,-X,+Y,-Y,+Z,-Z order, each
    // tightly packed width*height*4 bytes. The cube mirror of uploadToGpuTexture.
    SDL_GPUTexture* uploadCubemap(const std::array<const void*, 6>& faces,
                                  Uint32 width, Uint32 height);

    // Create the image-based-lighting bake resources (Step 15): the three bake pipelines
    // (the irradiance + prefilter cube convolutions, sharing ibl_cube.vert, and the
    // BRDF-LUT fullscreen pass), a trilinear CLAMP sampler to read the maps, and the three
    // target textures. The BRDF LUT is environment-independent, so it's baked here
    // immediately; the two cube convolutions are baked later by bakeIbl (they need a loaded
    // skybox). Called once from the ctor, after the skybox resources.
    bool createIblResources();

    // Bake the diffuse irradiance + specular prefilter cubemaps FROM skyboxCubemap_, by
    // rendering the cube into each face — and, for the prefilter, each roughness mip — with
    // ibl_cube.vert. One-time, called at the end of loadCubemap; sets iblReady_ so the
    // shader starts using the maps. Re-baking (a sky reload) is automatic.
    void bakeIbl();

    // Render the cube once into one (face, mip) subresource of `target` using `pipeline`
    // and the per-face `captureViewProj`. Shared by the irradiance and prefilter bakes.
    // When `prefilterParams` is non-null it's pushed as the fragment uniform (roughness +
    // env resolution) the prefilter shader reads; the irradiance shader takes none.
    void bakeCubeFace(SDL_GPUCommandBuffer* cmd, SDL_GPUGraphicsPipeline* pipeline,
                      SDL_GPUTexture* target, Uint32 face, Uint32 mip, Uint32 size,
                      const Mat4& captureViewProj, const float* prefilterParams) const;

    // Draw the skybox into the already-begun main render pass: bind the skybox
    // pipeline + cubemap, push the translation-stripped view-projection, and draw the
    // cube. Called at the END of recordScene so the sky fills only background pixels.
    void recordSkybox(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                      const Mat4& view, const Mat4& projection) const;

    // Build this frame's draw batches + instance buffers (Step 24), BEFORE any render
    // pass begins. Culls the queue to the camera → sorts the opaque survivors by
    // (material, mesh) and the whole queue (for shadows) by mesh → packs each item's
    // transform into a transient instance buffer → coalesces the sorted lists into
    // instanced DrawBatches. Uploads both buffers here (their copy runs on its own
    // command buffer, submitted before the frame's), so the record passes just draw.
    // Non-const: it fills the scratch/batch members and recreates the instance buffers.
    void buildFrameBatches(const Mat4& projView, const Vec3& cameraPos);

    // Recreate a transient per-frame instance buffer (release last frame's, upload the
    // new data) — the color pass's InstanceData[] and the shadow pass's Mat4[]. Same
    // immediate-mode pattern as uploadDebugLines: no two frames share a buffer.
    void uploadColorInstances(std::span<const InstanceData> instances);
    void uploadShadowInstances(std::span<const Mat4> models);

    // PASS 1 of shadow mapping: render the scene's depth from the light's point of
    // view into the shadow map. Begins its own depth-only render pass and draws the
    // shadow batches built by buildFrameBatches (the WHOLE queue, never camera-culled —
    // a caster outside the camera frustum can still drop a shadow into view), pushing
    // `lightViewProj` once. Done before the main pass.
    void renderShadowPass(SDL_GPUCommandBuffer* cmd, const Mat4& lightViewProj) const;

    // Bind the per-frame lighting inputs the scene shader reads, and push the light
    // uniform: the shadow map (fragment slot 4), the three IBL maps (slots 5–7), and
    // the packed LightUniform (fragment slot 0 — ambient/IBL flag, eye, sun matrix,
    // the active-light array). Factored out (Step 21) because it must run once PER
    // scene pipeline: binding a new pipeline resets the pass's SAMPLER bindings, so the
    // opaque and transparent passes each re-establish them. (Pushed uniform data
    // survives a pipeline switch, but re-pushing it here is cheap and keeps this the
    // single place per-frame lighting is set up.)
    void bindFrameLighting(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                           const Vec3& cameraPos, std::span<const Light> lights,
                           const Mat4& lightViewProj) const;

    // Record the whole scene into an already-begun (main) render pass from the batches
    // built by buildFrameBatches (Step 24): push the shared `viewProj` uniform, draw the
    // OPAQUE batches (depth-write on) → the skybox → the TRANSPARENT items back-to-front
    // through the blend pipeline (depth-write off). Takes `projection`/`view` for the
    // skybox and packs the per-frame lighting from `lights`/`cameraPos`/`lightViewProj`.
    // Shared by the live (renderFrame) and off-screen (captureFrame) paths so they can't
    // drift.
    void recordScene(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                     const Mat4& projection, const Mat4& view, const Vec3& cameraPos,
                     std::span<const Light> lights, const Mat4& lightViewProj) const;

    // Bind a material's five maps (albedo/metal-rough/normal/AO at fragment slots 0–3,
    // emissive at slot 8) + push its PBR/emissive/opacity params (fragment slot 1).
    // Shared by the opaque batches and the transparent items — the state a batch key
    // groups on. (Missing maps fall back to the neutral 1×1 textures.)
    void bindMaterial(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                      const Material& material) const;

    // Submit ONE instanced draw into the already-begun main render pass: bind the
    // material + the mesh's buffers + the color instance buffer at `firstInstance`, then
    // draw `count` instances. `count` is the batch length for an opaque run, or 1 for a
    // single transparent item — the "submit" half of traverse → list → batch → submit.
    void submitBatch(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                     const Mesh& mesh, const Material& material,
                     Uint32 firstInstance, Uint32 count) const;

    // Ensure the depth texture exists and matches (w, h), recreating it on resize.
    // Returns false (after logging) if creation fails. Called each frame before
    // the render pass, since the window — and thus the needed depth size — can
    // change. See docs/tuts/04 for what a depth buffer is and why we need one.
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
                            const SDL_FColor& clearColor, const PostSettings& post,
                            std::span<const DebugVertex> debugLines,
                            std::span<const HudVertex> hud);

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

    // The TRANSPARENT scene pipeline (Step 21). Identical to trianglePipeline_ — same
    // shaders, vertex layout, depth format — with two differences that make alpha
    // blending work: blending is ENABLED (the "over" operator, src·α + dst·(1-α)) and
    // depth WRITE is OFF (depth TEST stays on). Off-write means a translucent surface is
    // still hidden by nearer opaque geometry, yet doesn't stamp the depth buffer, so
    // other translucent surfaces behind it can still blend through. Built alongside
    // trianglePipeline_ from the same shaders; used for the back-to-front transparent pass.
    SDL_GPUGraphicsPipeline* transparentPipeline_ = nullptr;  // owned

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

    // Skybox / environment cubemap (Step 14). The scene is drawn first, then a unit
    // cube around the camera samples this cubemap by direction, pinned to the far
    // plane (LEQUAL, no depth write) so the sky fills only background pixels. The
    // cubemap is the groundwork for Step 15's image-based lighting. skyboxCubemap_ is
    // populated by loadCubemap; when it's null (no sky loaded) the sky draw is skipped.
    SDL_GPUGraphicsPipeline* skyboxPipeline_ = nullptr;  // owned
    SDL_GPUTexture*          skyboxCubemap_  = nullptr;  // owned (6-layer CUBE texture)
    SDL_GPUSampler*          skyboxSampler_  = nullptr;  // owned (CLAMP_TO_EDGE, no seams)
    std::shared_ptr<Mesh>    skyboxMesh_;                // the cube drawn around the camera
    bool                     skyboxEnabled_  = true;     // runtime show/hide toggle
    Uint32                   skyboxFaceSize_ = 0;        // per-face size (for the IBL prefilter)

    // Image-based lighting (Step 15). Three maps let the environment LIGHT the scene, not
    // just sit behind it. Baked ONCE from skyboxCubemap_ at load: a diffuse IRRADIANCE cube
    // (the sky cosine-convolved over the hemisphere), a specular PREFILTER cube (the sky
    // GGX-blurred, one roughness per mip), and the 2D BRDF LUT (the split-sum's
    // environment-independent factor — baked in createIblResources since it never changes).
    // triangle.frag reads all three for the ambient term. iblReady_ gates that until a bake
    // has actually run (a cubemap must exist); iblEnabled_ is the runtime A/B toggle.
    SDL_GPUGraphicsPipeline* iblIrradiancePipeline_ = nullptr;  // owned
    SDL_GPUGraphicsPipeline* iblPrefilterPipeline_  = nullptr;  // owned
    SDL_GPUGraphicsPipeline* iblBrdfPipeline_       = nullptr;  // owned
    SDL_GPUTexture*          iblIrradiance_         = nullptr;  // owned (CUBE, HDR, 1 mip)
    SDL_GPUTexture*          iblPrefilter_          = nullptr;  // owned (CUBE, HDR, roughness mips)
    SDL_GPUTexture*          iblBrdfLut_            = nullptr;  // owned (2D, RG float)
    SDL_GPUSampler*          iblSampler_            = nullptr;  // owned (linear, trilinear, CLAMP)
    bool                     iblReady_              = false;    // true once baked from a cubemap
    bool                     iblEnabled_            = true;     // runtime IBL on/off toggle

    // Render queue + frustum culling (Step 20). Each frame renderSceneAndPost
    // flattens the scene graph into `renderQueue_` (traverse → list), then the
    // shadow pass draws it all while the main pass draws only the items whose world
    // bounds survive the camera frustum, collected into `visibleItems_`. Both
    // vectors are members (not locals) so their capacity is REUSED frame to frame —
    // no per-frame allocation. frustumCullingEnabled_ is the runtime A/B toggle.
    std::vector<RenderItem>        renderQueue_;                // flat draw list, rebuilt per frame
    // These reused scratch buffers are filled each frame by buildFrameBatches (Step 24):
    // visibleItems_ holds the frustum survivors; partitionByBlend (Step 21) splits those
    // into opaque + (back-to-front) transparent lists; opaqueItems_ is then sorted by
    // (material, mesh) for batching. Members so their capacity is REUSED frame to frame.
    mutable std::vector<const RenderItem*> visibleItems_;      // scratch: items that passed culling
    mutable std::vector<const RenderItem*> opaqueItems_;       // scratch: opaque survivors (sorted by batch key)
    mutable std::vector<const RenderItem*> transparentItems_;  // scratch: blend survivors (back-to-front)
    bool                          frustumCullingEnabled_ = true;
    bool                          transparentSortEnabled_ = true;  // Step 21 back-to-front A/B toggle

    // Instancing / draw-call batching (Step 24). buildFrameBatches sorts the visible
    // items by batch key, packs each one's transform into a transient INSTANCE buffer,
    // and coalesces the sorted lists into instanced draws. Two buffers: the color pass
    // needs model + normalMatrix per instance (InstanceData), the shadow pass just the
    // model matrix (depth is material-blind). Rebuilt every frame like the overlays; the
    // CPU scratch vectors + batch lists reuse their capacity. lastDrawStats_ records the
    // items-vs-draw-calls gap the demo HUD shows.
    SDL_GPUBuffer* colorInstanceBuffer_  = nullptr;  // owned; recreated per frame (InstanceData[])
    SDL_GPUBuffer* shadowInstanceBuffer_ = nullptr;  // owned; recreated per frame (Mat4[])
    Uint32         colorInstanceCount_   = 0;        // instances uploaded this frame (opaque + transparent)
    Uint32         shadowInstanceCount_  = 0;        // instances uploaded this frame (whole queue)
    mutable std::vector<InstanceData>      colorInstances_;   // scratch: opaque(sorted)+transparent transforms
    mutable std::vector<Mat4>              shadowInstances_;  // scratch: whole-queue model matrices
    mutable std::vector<const RenderItem*> shadowOrder_;      // scratch: the queue sorted by mesh
    mutable std::vector<DrawBatch>         opaqueBatches_;    // scratch: coalesced (material,mesh) runs
    mutable std::vector<DrawBatch>         shadowBatches_;    // scratch: coalesced mesh runs
    mutable DrawStats                      lastDrawStats_;

    // Debug draw (Step 22). An unlit line pipeline overlays throwaway wireframes
    // (AABBs, the camera frustum, light icons) on the scene. The vertex buffer is
    // rebuilt every frame from the FrameView's debug lines — immediate mode — so it
    // holds no state between frames; debugVertexCount_ is this frame's line-vertex
    // count. lastCameraViewProj_ caches the main pass's projection·view so the app
    // can freeze and re-draw the culling frustum (see lastCameraViewProjection()).
    SDL_GPUGraphicsPipeline* debugLinePipeline_  = nullptr;  // owned
    SDL_GPUBuffer*           debugVertexBuffer_  = nullptr;  // owned; recreated per frame
    Uint32                   debugVertexCount_   = 0;        // vertices uploaded this frame
    mutable Mat4             lastCameraViewProj_ = Mat4::identity();  // set in recordScene (const)

    // HUD / text overlay (Step 23). A textured pipeline draws 2D screen-space quads —
    // text glyphs and filled panels — onto the FINAL image, after post-processing, so
    // the HUD stays crisp (no tone-map/FXAA blur). hudAtlas_ is the baked 8x8 bitmap
    // font atlas (Font.hpp); hudSampler_ reads it with NEAREST filtering for sharp
    // pixels. Like debug draw, the vertex buffer is rebuilt every frame from the
    // FrameView's HUD vertices — immediate mode, no state between frames.
    SDL_GPUGraphicsPipeline* hudPipeline_     = nullptr;  // owned
    std::shared_ptr<Texture> hudAtlas_;                   // baked font atlas (RGBA)
    SDL_GPUSampler*          hudSampler_      = nullptr;  // owned (nearest, clamp)
    SDL_GPUBuffer*           hudVertexBuffer_ = nullptr;  // owned; recreated per frame
    Uint32                   hudVertexCount_  = 0;        // vertices uploaded this frame

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
