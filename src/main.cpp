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

// Exposes build counts and player state to the page, for the smoke test.
extern "C" EMSCRIPTEN_KEEPALIVE int museumRoomCount() {
  return g_engine.museum().stats().rooms;
}

extern "C" EMSCRIPTEN_KEEPALIVE int museumColliderCount() {
  return g_engine.museum().stats().colliders;
}

extern "C" EMSCRIPTEN_KEEPALIVE int museumSegmentCount() {
  return g_engine.museum().stats().segments;
}

extern "C" EMSCRIPTEN_KEEPALIVE int museumPaintingSpotCount() {
  return g_engine.museum().stats().painting_spots;
}

extern "C" EMSCRIPTEN_KEEPALIVE float museumPlayerX() {
  return g_engine.player().position().x;
}

extern "C" EMSCRIPTEN_KEEPALIVE float museumPlayerY() {
  return g_engine.player().position().y;
}

extern "C" EMSCRIPTEN_KEEPALIVE float museumPlayerZ() {
  return g_engine.player().position().z;
}

extern "C" EMSCRIPTEN_KEEPALIVE float museumEyeY() {
  return g_engine.player().position().y + museum::player::kCameraEyeOffsetY;
}

extern "C" EMSCRIPTEN_KEEPALIVE int museumOnGround() {
  return g_engine.player().on_ground() ? 1 : 0;
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
