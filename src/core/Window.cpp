#include "core/Window.hpp"

#include "core/Log.hpp"

namespace koi {

Window::Window(const char* title, int width, int height) {
    // SDL_CreateWindow(title, width, height, flags).
    //   - SDL_WINDOW_RESIZABLE lets the user drag the window edges. We handle
    //     the resulting resize events in Engine, and the swapchain adapts.
    //   - We intentionally do NOT pass a renderer/OpenGL flag here: the SDL3
    //     GPU API attaches itself to the window later via ClaimWindow, so a
    //     plain window is exactly what we want.
    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);

    if (window_ == nullptr) {
        // SDL_GetError() returns a human-readable string describing the most
        // recent SDL failure on this thread — always log it after an SDL call
        // returns null/false.
        KOI_ERROR("Failed to create window: %s", SDL_GetError());
        return;
    }

    KOI_INFO("Created window '%s' (%dx%d)", title, width, height);
}

Window::~Window() {
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

}  // namespace koi
