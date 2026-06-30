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
//  See docs/01-window-and-render-loop.html for the GPU concepts named below and
//  docs/06-meshes-and-scene-graph.html for meshes + the scene graph.
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

#include <memory>  // std::shared_ptr — meshes are shared, owned via shared_ptr
#include <span>    // std::span — a (pointer, length) view over caller-owned geometry

#include "math/Mat4.hpp"
#include "renderer/Vertex.hpp"  // createMesh takes a std::span<const Vertex>

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
        return device_ != nullptr && trianglePipeline_ != nullptr && sampler_ != nullptr;
    }

    // Upload geometry into a new Mesh and return it (shared, so many scene nodes
    // can reference one Mesh). `vertices`/`indices` are copied into GPU buffers
    // via the staging-upload path; indices are 16-bit (Uint16). Returns nullptr
    // (after logging) on failure. This is the ONLY place meshes are born, because
    // it's the only thing holding both the device and the upload helper.
    [[nodiscard]] std::shared_ptr<Mesh> createMesh(std::span<const Vertex> vertices,
                                                   std::span<const Uint16> indices);

    // Load an image file (BMP) from `path` and upload it into a new GPU Texture
    // (shared, so it could be reused by many draws). Returns nullptr (after logging)
    // on failure. Like createMesh, this is the only place textures are born — it
    // holds both the device and the upload helper.
    [[nodiscard]] std::shared_ptr<Texture> loadTexture(const char* path);

    // Render exactly one frame: acquire a swapchain image, clear it (and the depth
    // buffer), draw every mesh in `sceneRoot` (each through its own world matrix and
    // its node's material) as seen through the camera `view`, and present it.
    // `cameraPos` (the eye in world space) feeds the lighting's specular highlight.
    void renderFrame(const SDL_FColor& clearColor, const Mat4& view,
                     const Node& sceneRoot, const Vec3& cameraPos);

    // Render one frame into an OFF-SCREEN texture (not the window), download the
    // pixels back to the CPU, and save them to `path` as a BMP. Our headless
    // visual-debugging tool: it captures exactly what the engine draws (the same
    // `sceneRoot` + `texture` through `view`) without a visible window. Returns
    // false (after logging) on failure. See docs / CLAUDE.md.
    [[nodiscard]] bool captureFrame(const char* path, const SDL_FColor& clearColor,
                                    const Mat4& view, const Node& sceneRoot,
                                    const Vec3& cameraPos);

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

    // Create a 2D sampled texture (R8G8B8A8_UNORM) of (width, height) and upload
    // `pixels` (tightly packed RGBA, 4 bytes/texel) into it via a staging transfer
    // buffer + copy pass — the 2D mirror of uploadToGpuBuffer. Returns nullptr
    // (after logging) on failure. The helper behind loadTexture.
    SDL_GPUTexture* uploadToGpuTexture(const void* pixels, Uint32 width, Uint32 height);

    // Create the one sampler all textures are read through (linear filtering,
    // REPEAT wrap so tiled UVs work). Called once from the constructor.
    bool createSampler();

    // Record the whole scene into an already-begun render pass: bind the pipeline,
    // push the per-frame light uniform (using `cameraPos`), build the projection
    // from `aspect`, then walk `root` drawing each node — binding its material's
    // texture + pushing its {mvp, model} and material uniforms. Shared by the live
    // (renderFrame) and off-screen (captureFrame) paths so they can't drift. `cmd`
    // pushes the uniforms.
    void recordScene(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                     const Node& root, const Mat4& view, float aspect,
                     const Vec3& cameraPos) const;

    // Recursive worker for recordScene: draw `node`'s mesh (if any) using the
    // already-combined `projView` matrix, then recurse into its children. Reads
    // the node's cached world matrix (filled by Node::updateWorldTransforms).
    void recordNode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass,
                    const Node& node, const Mat4& projView) const;

    // Ensure the depth texture exists and matches (w, h), recreating it on resize.
    // Returns false (after logging) if creation fails. Called each frame before
    // the render pass, since the window — and thus the needed depth size — can
    // change. See docs/04 for what a depth buffer is and why we need one.
    bool ensureDepthTexture(Uint32 width, Uint32 height);

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

    // The depth buffer: a texture the same size as the color target that stores,
    // per pixel, the depth of the nearest fragment drawn so far. With the depth
    // test enabled, the GPU keeps a new fragment only if it's closer — that's how
    // near faces correctly hide far ones, regardless of draw order. Created lazily
    // and resized to match the swapchain.
    SDL_GPUTexture*      depthTexture_ = nullptr;  // owned
    Uint32              depthWidth_   = 0;
    Uint32              depthHeight_  = 0;
    SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;  // chosen at pipeline build
};

}  // namespace koi
