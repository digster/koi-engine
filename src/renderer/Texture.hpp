// ============================================================================
//  Texture.hpp — an image living in GPU memory, ready to be sampled
// ----------------------------------------------------------------------------
//  A texture is a grid of texels (texture pixels) the GPU can read inside a
//  shader. Step 6 loads an image file, uploads its pixels into one of these, and
//  the fragment shader samples it to color each surface.
//
//  Like Mesh, a Texture is a thin RAII owner of one GPU resource. It holds a
//  NON-OWNING SDL_GPUDevice* purely to release the texture in its destructor, so
//  the same lifetime rule applies: every Texture must be destroyed BEFORE the
//  GpuRenderer tears the device down. The Engine guarantees this by releasing the
//  texture (and the scene) before the renderer in shutdown().
//
//  A Texture owns only the image data, NOT a sampler. A *sampler* describes HOW to
//  read the texture (filtering between texels, what happens past the edges); it's
//  reusable device state, so the renderer owns one shared sampler and pairs it with
//  whatever Texture it draws (see GpuRenderer). Textures are created via
//  GpuRenderer::loadTexture, held by shared_ptr, and are non-copyable/non-movable.
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

namespace koi {

class Texture {
public:
    // Takes ownership of an already-uploaded GPU texture. `device` is borrowed, not
    // owned — it must outlive this Texture. Use GpuRenderer::loadTexture, not this.
    Texture(SDL_GPUDevice* device, SDL_GPUTexture* texture, Uint32 width, Uint32 height);

    // Releases the GPU texture via the (borrowed) device.
    ~Texture();

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&)                 = delete;
    Texture& operator=(Texture&&)      = delete;

    [[nodiscard]] SDL_GPUTexture* handle() const { return texture_; }
    [[nodiscard]] Uint32          width()  const { return width_; }
    [[nodiscard]] Uint32          height() const { return height_; }

private:
    SDL_GPUDevice*  device_  = nullptr;  // borrowed — owned by GpuRenderer
    SDL_GPUTexture* texture_ = nullptr;  // owned
    Uint32          width_   = 0;
    Uint32          height_  = 0;
};

}  // namespace koi
