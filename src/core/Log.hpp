// ============================================================================
//  Log.hpp — tiny leveled logging layer for Koi Engine
// ----------------------------------------------------------------------------
//  Why have this at all? Two reasons:
//    1. A single place to control verbosity (Debug builds talk a lot, Release
//       builds stay quiet) instead of scattering printf calls everywhere.
//    2. Log *levels* let us say HOW important a message is. ERROR means "this
//       broke", INFO means "here's what happened", DEBUG means "internal detail
//       only useful while developing".
//
//  We build on SDL's logging (SDL_LogInfo, SDL_LogError, ...) rather than
//  std::cout because SDL already routes output sensibly on every platform
//  (e.g. the debugger console on Windows, stderr on macOS/Linux) and uses the
//  same printf-style format strings throughout.
// ============================================================================
#pragma once

#include <SDL3/SDL_log.h>

namespace koi::log {

// SDL groups log messages into "categories" (video, audio, render, ...). We
// route all engine messages through APPLICATION — the category SDL intends for
// app-level logging. It defaults to INFO priority (so our INFO lines show even
// before/after we tweak priorities), and our init() below raises it to DEBUG in
// verbose mode. (You could instead define a category >= SDL_LOG_CATEGORY_CUSTOM
// for finer-grained control, but those default to ERROR and are easy to silence
// accidentally — e.g. SDL_Quit resets priorities.)
inline constexpr int kCategory = SDL_LOG_CATEGORY_APPLICATION;

// Configure how chatty the logger is. Call once at startup.
//   verbose = true  -> show DEBUG and above (development).
//   verbose = false -> show INFO and above  (normal runs).
inline void init(bool verbose) {
    SDL_SetLogPriority(kCategory,
        verbose ? SDL_LOG_PRIORITY_DEBUG : SDL_LOG_PRIORITY_INFO);
}

}  // namespace koi::log

// Convenience macros. Macros (not functions) so the printf-style variadic
// format string + arguments pass straight through to SDL untouched, and so the
// call site stays short: KOI_INFO("Window %dx%d", w, h).
#define KOI_DEBUG(...) SDL_LogDebug(koi::log::kCategory, __VA_ARGS__)
#define KOI_INFO(...)  SDL_LogInfo(koi::log::kCategory, __VA_ARGS__)
#define KOI_WARN(...)  SDL_LogWarn(koi::log::kCategory, __VA_ARGS__)
#define KOI_ERROR(...) SDL_LogError(koi::log::kCategory, __VA_ARGS__)
