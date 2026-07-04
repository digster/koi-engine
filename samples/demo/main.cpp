// ============================================================================
//  samples/demo/main.cpp — entry point for the Koi demo application
// ----------------------------------------------------------------------------
//  This is a *client* of the engine, living outside src/. It shows the whole
//  contract: create an Engine, initialise it, build an Application, and run it.
//  All the demo's content and behaviour live in DemoApp — main() only wires the
//  two together and translates the outcome into a process exit code. A real game
//  would look exactly like this, with its own Application in place of DemoApp.
//
//  We include <SDL3/SDL_main.h> because on some platforms SDL provides its own
//  entry point and renames ours; including it lets SDL do that transparently
//  while we still write a normal-looking main().
// ============================================================================
#include <SDL3/SDL_main.h>

#include "DemoApp.hpp"
#include "core/Engine.hpp"

int main(int /*argc*/, char* /*argv*/[]) {
    koi::Engine engine;

    const koi::Engine::Config config{
        .title  = "Koi Engine — Step 17: engine/app separation",
        .width  = 1280,
        .height = 720,
    };

    // If initialization fails, the engine has already logged why; just exit.
    if (!engine.init(config)) {
        return 1;
    }

    // The app owns all content/behaviour; the engine drives it. run() calls the
    // app's onStart, loops (or captures a single frame under KOI_CAPTURE), and
    // calls the app's onShutdown before returning — while the renderer is alive.
    koi::DemoApp app;
    const bool ok = engine.run(app);

    engine.shutdown();  // explicit, though ~Engine would also handle it
    return ok ? 0 : 1;
}
