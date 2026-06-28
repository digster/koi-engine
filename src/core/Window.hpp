// ============================================================================
//  Window.hpp — an RAII wrapper around an SDL_Window
// ----------------------------------------------------------------------------
//  "RAII" (Resource Acquisition Is Initialization) is the core C++ idiom for
//  managing resources: a resource's lifetime is tied to an object's lifetime.
//  The constructor acquires the resource (creates the OS window); the
//  destructor releases it (destroys the window). When a Window object goes out
//  of scope, the OS window is cleaned up automatically — no manual "remember to
//  destroy it" bookkeeping, even if an error causes an early return.
//
//  The window is also where our future rendering surface lives: the GPU's
//  "swapchain" (see GpuRenderer) is attached to this window.
// ============================================================================
#pragma once

#include <SDL3/SDL.h>

namespace koi {

class Window {
public:
    // Creating the object creates the OS window. If creation fails, handle()
    // returns nullptr and isValid() returns false (we check this in Engine
    // rather than throwing, to keep the control flow simple and explicit).
    Window(const char* title, int width, int height);

    // Destroying the object destroys the OS window.
    ~Window();

    // A window owns a unique OS resource, so copying it makes no sense — two
    // Window objects must never think they both own the same SDL_Window.
    // Deleting these makes accidental copies a compile error.
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // The raw SDL handle, needed by the renderer to attach its swapchain.
    [[nodiscard]] SDL_Window* handle() const { return window_; }

    [[nodiscard]] bool isValid() const { return window_ != nullptr; }

private:
    SDL_Window* window_ = nullptr;
};

}  // namespace koi
