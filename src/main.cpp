// ============================================================================
//  main.cpp — program entry point
// ----------------------------------------------------------------------------
//  Deliberately tiny: all the real work lives in the Engine. main() just builds
//  a configuration, hands it to the engine, and translates the outcome into a
//  process exit code.
//
//  We include <SDL3/SDL_main.h> because on some platforms SDL needs to provide
//  its own entry point and rename ours (e.g. to set up the app environment).
//  Including it here lets SDL do that transparently while we still write a
//  normal-looking main().
// ============================================================================
#include <SDL3/SDL_main.h>

#include "core/Engine.hpp"

int main(int /*argc*/, char* /*argv*/[]) {
    koi::Engine engine;

    const koi::Engine::Config config{
        .title  = "Koi Engine — Step 3: 3D Cube (MVP + Depth)",
        .width  = 1280,
        .height = 720,
    };

    // If initialization fails, the engine has already logged why; just exit.
    if (!engine.init(config)) {
        return 1;
    }

    engine.run();       // blocks until the user quits
    engine.shutdown();  // explicit, though ~Engine would also handle it
    return 0;
}
