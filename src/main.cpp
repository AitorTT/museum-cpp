// Museum - entry point and per-platform loop.
//
// The museum itself lives in Engine; this file only differs by platform:
//   desktop  SDL3 drives a while-loop on the main thread
//   web      the browser drives requestAnimationFrame
#include <cstdio>

#include "engine.h"

using museum::Engine;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
namespace {
Engine g_engine;
void Frame() { g_engine.Frame(); }
}  // namespace
#endif

#ifdef __EMSCRIPTEN__

// The page calls this once the canvas exists. Kept extern "C" so the JS bridge
// in web/shell.html can reach it by name.
extern "C" EMSCRIPTEN_KEEPALIVE void museumStart() {
  if (!g_engine.Initialize("Museum", 1280, 720)) {
    std::printf("Museum: initialization failed\n");
    return;
  }
  std::printf("Museum: running\n");
  emscripten_set_main_loop(Frame, 0, true);
}

#else

int main() {
  static Engine engine;
  if (!engine.Initialize("Museum", 1280, 720)) {
    return 1;
  }
  std::printf("Museum: running (click the window to look around)\n");

  while (!engine.quit_requested()) {
    engine.Frame();
  }
  return 0;
}

#endif
