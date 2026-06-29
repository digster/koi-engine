#include "renderer/Texture.hpp"

namespace koi {

Texture::Texture(SDL_GPUDevice* device, SDL_GPUTexture* texture,
                 Uint32 width, Uint32 height)
    : device_(device), texture_(texture), width_(width), height_(height) {}

Texture::~Texture() {
    // Release the texture we own through the borrowed device. SDL defers the real
    // free until the GPU is done with it, so this is safe even mid-frame — provided
    // the device is still alive, which the Engine's teardown order guarantees
    // (texture released before the renderer). Releasing null is a harmless no-op.
    if (device_ != nullptr) {
        SDL_ReleaseGPUTexture(device_, texture_);
    }
}

}  // namespace koi
