#include "scene/Camera.hpp"

#include <cmath>  // std::sin, std::cos

#include <SDL3/SDL.h>  // SDL_Scancode constants for the keyboard-state array

namespace koi {

// The world's "up". We keep up fixed (not the camera's own up) so movement and
// turning behave predictably — a so-called "FPS" camera rather than a free
// 6-DOF one.
namespace {
constexpr Vec3 kWorldUp = {0.0f, 1.0f, 0.0f};
}

Vec3 Camera::forward() const {
    // Spherical-to-Cartesian: yaw sweeps around the up axis, pitch tilts up/down.
    // With yaw = -90°, this points down -Z; pitch = ±90° would look straight
    // up/down (we clamp short of that in addMouseLook).
    const float y = radians(yaw_);
    const float p = radians(pitch_);
    return normalize(Vec3{
        std::cos(p) * std::cos(y),
        std::sin(p),
        std::cos(p) * std::sin(y),
    });
}

Mat4 Camera::viewMatrix() const {
    // Look from our position toward a point one unit ahead along `forward`.
    return lookAt(position_, position_ + forward(), kWorldUp);
}

void Camera::processKeyboard(const bool* keys, float dt) {
    if (keys == nullptr) {
        return;
    }
    const Vec3 fwd   = forward();
    const Vec3 right = normalize(cross(fwd, kWorldUp));  // strafe direction
    const float step = moveSpeed_ * dt;                  // distance this frame

    // WASD move in the look plane; E/Q rise/fall along world-up (a "fly" camera).
    if (keys[SDL_SCANCODE_W]) position_ = position_ + fwd   * step;
    if (keys[SDL_SCANCODE_S]) position_ = position_ - fwd   * step;
    if (keys[SDL_SCANCODE_D]) position_ = position_ + right * step;
    if (keys[SDL_SCANCODE_A]) position_ = position_ - right * step;
    if (keys[SDL_SCANCODE_E]) position_ = position_ + kWorldUp * step;
    if (keys[SDL_SCANCODE_Q]) position_ = position_ - kWorldUp * step;
}

void Camera::addMouseLook(float dx, float dy) {
    yaw_   += dx * mouseSensitivity_;
    pitch_ -= dy * mouseSensitivity_;  // invert Y so pushing the mouse up looks up

    // Clamp pitch just short of straight up/down: at exactly ±90° the forward
    // vector aligns with world-up and the view "gimbal locks" / flips.
    if (pitch_ >  89.0f) pitch_ =  89.0f;
    if (pitch_ < -89.0f) pitch_ = -89.0f;
}

}  // namespace koi
