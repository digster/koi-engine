// ============================================================================
//  Camera.hpp — a first-person "fly" camera
// ----------------------------------------------------------------------------
//  A camera is just a position and an orientation, from which we derive a VIEW
//  matrix (see Mat4::lookAt). This is a classic Euler-angle fly camera:
//
//    * position : where the camera is in the world.
//    * yaw      : left/right turn  (rotation about the world's up axis).
//    * pitch    : up/down tilt     (rotation about the camera's right axis).
//
//  From yaw + pitch we compute a `forward` direction; the view matrix then maps
//  the world into the camera's frame. We deliberately keep this in `scene/`
//  (above the renderer): the camera describes *what we're looking at*, and hands
//  the renderer a view matrix — it knows nothing about GPUs.
//
//  This header is intentionally SDL-free (math types only) so it can be included
//  cheaply (e.g. by Engine) without pulling in SDL. The keyboard handling in the
//  .cpp uses SDL scancodes, but the interface only speaks `const bool*`.
// ============================================================================
#pragma once

#include "math/Mat4.hpp"
#include "math/Vec.hpp"

namespace koi {

class Camera {
public:
    // Default pose: pulled back along +Z and slightly up, tilted a touch down so
    // the cube cluster near the origin sits nicely in frame.
    Camera() = default;

    // The view matrix to hand the renderer: transforms world space into this
    // camera's space (it's the camera's inverse — see Mat4::lookAt).
    [[nodiscard]] Mat4 viewMatrix() const;

    // The unit direction the camera is looking, derived from yaw + pitch.
    [[nodiscard]] Vec3 forward() const;

    [[nodiscard]] Vec3 position() const { return position_; }

    // Move based on which keys are currently held. `keys` is the array returned
    // by SDL_GetKeyboardState (indexed by scancode); we read it each frame rather
    // than reacting to key-down EVENTS, so movement is smooth and continuous.
    // `dt` (seconds) scales the motion so speed is frame-rate independent.
    void processKeyboard(const bool* keys, float dt);

    // Apply a relative mouse movement (pixels) to yaw/pitch for FPS-style look.
    // Pitch is clamped so you can't flip over the top (which would invert "up").
    void addMouseLook(float dx, float dy);

private:
    Vec3  position_         = {0.0f, 2.0f, 9.0f};
    float yaw_              = -90.0f;  // degrees; -90° faces down -Z initially
    float pitch_            = -10.0f;  // degrees; slight downward tilt
    float moveSpeed_        = 6.0f;    // units per second
    float mouseSensitivity_ = 0.12f;   // degrees of turn per pixel of mouse motion
};

}  // namespace koi
