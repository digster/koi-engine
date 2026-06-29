// ============================================================================
//  test_camera.cpp — unit tests for the fly camera's math & input response
// ----------------------------------------------------------------------------
//  The Camera is mostly pure logic: given a key-state array and a delta-time, it
//  updates a position and orientation. That makes the interesting parts testable
//  headlessly — no window, no SDL_Init, just the scancode CONSTANTS (which are
//  compile-time enum values). We feed a stack-allocated key array exactly like
//  the one SDL_GetKeyboardState returns.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include <array>
#include <cmath>  // std::sin

#include <SDL3/SDL.h>  // SDL_SCANCODE_* constants + SDL_SCANCODE_COUNT

#include "math/Vec.hpp"
#include "scene/Camera.hpp"

using namespace koi;

namespace {
// A zeroed key-state array sized like SDL's (indexable by any scancode).
using KeyArray = std::array<bool, SDL_SCANCODE_COUNT>;
}  // namespace

TEST_CASE("Camera's default orientation looks down -Z") {
    const Camera cam;
    const Vec3 f = cam.forward();
    CHECK(length(f) == doctest::Approx(1.0f));            // it's a unit direction
    CHECK(f.x == doctest::Approx(0.0f).epsilon(0.01));    // yaw -90° → no sideways lean
    CHECK(f.z < 0.0f);                                    // pointing into the screen (-Z)
}

TEST_CASE("Holding W moves the camera along its forward direction") {
    Camera cam;
    const Vec3 before = cam.position();
    const Vec3 f = cam.forward();

    KeyArray keys{};
    keys[SDL_SCANCODE_W] = true;
    cam.processKeyboard(keys.data(), /*dt=*/0.5f);

    const Vec3 disp = cam.position() - before;
    CHECK(length(disp) > 0.0f);              // it actually moved
    const Vec3 dir = normalize(disp);        // ...and in the look direction
    CHECK(dir.x == doctest::Approx(f.x));
    CHECK(dir.y == doctest::Approx(f.y));
    CHECK(dir.z == doctest::Approx(f.z));
}

TEST_CASE("S moves opposite to W") {
    Camera fwd;
    Camera back;
    KeyArray wKeys{};
    wKeys[SDL_SCANCODE_W] = true;
    KeyArray sKeys{};
    sKeys[SDL_SCANCODE_S] = true;

    const Vec3 start = fwd.position();
    fwd.processKeyboard(wKeys.data(), 0.5f);
    back.processKeyboard(sKeys.data(), 0.5f);

    // W and S from the same start should land on opposite sides of it.
    CHECK((fwd.position().z - start.z) < 0.0f);   // forward is -Z
    CHECK((back.position().z - start.z) > 0.0f);
}

TEST_CASE("E rises along world up") {
    Camera cam;
    const float y0 = cam.position().y;
    KeyArray keys{};
    keys[SDL_SCANCODE_E] = true;
    cam.processKeyboard(keys.data(), 0.5f);
    CHECK(cam.position().y > y0);
}

TEST_CASE("Mouse look clamps pitch so the view can't flip over the top") {
    Camera cam;
    cam.addMouseLook(/*dx=*/0.0f, /*dy=*/-100000.0f);  // yank the view far up

    // Pitch is clamped to +89°, so forward.y tops out at sin(89°) ≈ 0.9998 and
    // never reaches 1 (straight up), which would gimbal-lock the view.
    const Vec3 f = cam.forward();
    CHECK(f.y < 1.0f);
    CHECK(f.y == doctest::Approx(std::sin(radians(89.0f))).epsilon(0.001));
}
