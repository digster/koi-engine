// ============================================================================
//  Mesh.hpp — a reusable chunk of geometry living in GPU memory
// ----------------------------------------------------------------------------
//  Until Step 5 the renderer owned ONE hardcoded cube: a single vertex buffer
//  and index buffer baked into GpuRenderer. A Mesh lifts that geometry out into
//  its own object so the engine can have *many* different shapes — and so many
//  scene nodes can share the same one (every cube in the scene points at a
//  single cube Mesh; we upload its 8 vertices once, not once per cube).
//
//  WHAT A MESH HOLDS
//    * a vertex buffer — the raw corner data (position + color), in GPU memory.
//    * an index buffer — which corners each triangle uses (see Vertex.hpp and
//      docs/03 for why indexing pays off).
//    * indexCount — how many indices to draw.
//  All three already existed inside GpuRenderer; Mesh just gives them an owner
//  with a clear lifetime. We standardise on 16-bit (Uint16) indices, which the
//  renderer relies on when it binds the index buffer.
//
//  OWNERSHIP & THE LIFETIME RULE (important, and a recurring RAII lesson)
//  A Mesh OWNS its two GPU buffers and releases them in its destructor. But GPU
//  buffers are freed *through the device that created them*, and the Mesh does
//  NOT own that device (GpuRenderer does). So the Mesh keeps a NON-OWNING
//  SDL_GPUDevice* and trusts that the device outlives it. The consequence:
//  every Mesh must be destroyed BEFORE the GpuRenderer tears the device down.
//  The Engine guarantees this by releasing the scene (which holds the meshes)
//  before the renderer — see Engine::shutdown().
//
//  Meshes are created via GpuRenderer::createMesh (the only place that has both
//  the device and the staging-upload helper). They are non-copyable and
//  non-movable: copying would double-free the buffers, and there is no need to
//  move a heap object we always hand around by shared_ptr.
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

namespace koi {

class Mesh {
public:
    // Takes ownership of two already-uploaded GPU buffers. `device` is borrowed,
    // not owned — it must outlive this Mesh (see the lifetime rule above).
    // Normally you don't call this directly: use GpuRenderer::createMesh.
    Mesh(SDL_GPUDevice* device, SDL_GPUBuffer* vertexBuffer,
         SDL_GPUBuffer* indexBuffer, Uint32 indexCount);

    // Releases the two GPU buffers via the (borrowed) device.
    ~Mesh();

    // Owns raw GPU handles → forbid copies (double-free) and moves (not needed;
    // always held by shared_ptr).
    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&)                 = delete;
    Mesh& operator=(Mesh&&)      = delete;

    [[nodiscard]] SDL_GPUBuffer* vertexBuffer() const { return vertexBuffer_; }
    [[nodiscard]] SDL_GPUBuffer* indexBuffer()  const { return indexBuffer_; }
    [[nodiscard]] Uint32         indexCount()   const { return indexCount_; }

private:
    SDL_GPUDevice* device_       = nullptr;  // borrowed — owned by GpuRenderer
    SDL_GPUBuffer* vertexBuffer_ = nullptr;  // owned
    SDL_GPUBuffer* indexBuffer_  = nullptr;  // owned
    Uint32         indexCount_   = 0;
};

}  // namespace koi
