// ============================================================================
//  test_application.cpp — the engine/app boundary (Step 17)
// ----------------------------------------------------------------------------
//  Step 17 split the engine (loop + services) from the app (content + behaviour)
//  behind the koi::Application interface and the koi::FrameView bundle. The loop
//  itself needs a real GPU device, so it can't be unit-tested headlessly — but
//  the *shape* of the boundary can: that the hooks override and dispatch through
//  a base pointer, that the default hooks are safe no-ops, and that a FrameView
//  round-trips the per-frame render inputs. That's exactly the "compiles clean,
//  wires wrong" class of mistake a unit test should catch cheaply.
//
//  Headless: no SDL_Init, no window. A default-constructed koi::Engine never
//  initialises SDL and its destructor is a safe no-op, so we can pass one to the
//  hooks that require an Engine& without touching the GPU.
//
//  No DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN here — test_core.cpp provides main().
// ============================================================================
#include <doctest/doctest.h>

#include "core/Application.hpp"
#include "core/Engine.hpp"

namespace {

// A minimal Application: it draws nothing (root stays null) and just records that
// its hooks were called, so we can assert the virtual dispatch works end-to-end.
class DummyApp final : public koi::Application {
public:
    int   started = 0;
    int   updates = 0;
    float lastDt  = -1.0f;

    bool onStart(koi::Engine& /*engine*/) override {
        ++started;
        return true;
    }
    void onUpdate(koi::Engine& /*engine*/, float dt) override {
        ++updates;
        lastDt = dt;
    }
    [[nodiscard]] koi::FrameView frameView() const override {
        koi::FrameView fv{};
        fv.clearColor = {0.25f, 0.5f, 0.75f, 1.0f};
        fv.cameraPos  = {1.0f, 2.0f, 3.0f};
        return fv;  // root left null: this app draws nothing
    }
    // onEvent / onShutdown deliberately left as the base-class defaults.
};

}  // namespace

TEST_CASE("Application hooks dispatch polymorphically through the base interface") {
    koi::Engine engine;             // default-constructed: never init'd, safe headless
    DummyApp app;
    koi::Application* base = &app;  // exercise the actual virtual boundary

    CHECK(base->onStart(engine) == true);
    CHECK(app.started == 1);

    base->onUpdate(engine, 0.016f);
    base->onUpdate(engine, 0.032f);
    CHECK(app.updates == 2);
    CHECK(app.lastDt == doctest::Approx(0.032f));
}

TEST_CASE("Default Application hooks are safe no-ops") {
    koi::Engine engine;
    DummyApp app;
    koi::Application* base = &app;

    // The base supplies no-op onEvent / onShutdown, so overriding them is optional.
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    base->onEvent(engine, event);  // must not touch the (uninitialised) engine
    base->onShutdown();            // must be safe to call
    CHECK(true);                   // reaching here without a crash is the assertion
}

TEST_CASE("FrameView carries the app's per-frame render inputs") {
    const DummyApp app;
    const koi::FrameView fv = app.frameView();

    CHECK(fv.root == nullptr);  // this app draws nothing
    CHECK(fv.clearColor.r == doctest::Approx(0.25f));
    CHECK(fv.clearColor.g == doctest::Approx(0.5f));
    CHECK(fv.cameraPos.x == doctest::Approx(1.0f));
    CHECK(fv.cameraPos.z == doctest::Approx(3.0f));
    CHECK(fv.lights.empty());  // a default span is empty
}
