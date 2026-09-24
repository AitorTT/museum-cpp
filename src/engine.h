// The engine: owns the platform, the player and the renderer, and runs the
// loop. Deliberately small. The museum world in Batch 2 plugs in between
// Update and Render without this file growing much.
#pragma once

#include "core/clock.h"
#include "platform/window.h"
#include "player/player.h"
#include "render/renderer.h"

namespace museum {

class Engine {
 public:
  Engine() = default;

  bool Initialize(const char* title, std::uint32_t width, std::uint32_t height);

  // One frame: input -> simulation -> render. Called by the platform loop
  // (SDL3's while-loop on desktop, requestAnimationFrame on the web).
  void Frame();

  bool quit_requested() const { return window_.quit_requested(); }
  bool ready() const { return renderer_.ready(); }

  // True once Initialize has returned successfully.
  bool running() const { return initialized_; }

 private:
  void HandleResize();

  platform::Window window_;
  core::Clock clock_;
  player::Player player_;
  render::Renderer renderer_;
  bool initialized_ = false;
};

}  // namespace museum
