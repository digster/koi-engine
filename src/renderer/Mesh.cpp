#include "renderer/Mesh.hpp"

namespace koi {

Mesh::Mesh(SDL_GPUDevice* device, SDL_GPUBuffer* vertexBuffer,
           SDL_GPUBuffer* indexBuffer, Uint32 indexCount)
    : device_(device),
      vertexBuffer_(vertexBuffer),
      indexBuffer_(indexBuffer),
      indexCount_(indexCount) {}

Mesh::~Mesh() {
    // Release the buffers we own through the borrowed device. SDL defers the
    // actual free until the GPU is no longer using them, so this is safe even if
    // a frame referencing this mesh is still in flight — provided the device is
    // still alive, which the Engine's teardown order guarantees (scene before
    // renderer). Releasing a null handle is a harmless no-op in SDL.
    if (device_ != nullptr) {
        SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
        SDL_ReleaseGPUBuffer(device_, indexBuffer_);
    }
}

}  // namespace koi
