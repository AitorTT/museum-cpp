// The engine: owns the museum, the platform, the player and the renderer, and
// runs the loop.
#pragma once

#include "core/clock.h"
#include "platform/window.h"
#include "player/player.h"
#include "render/renderer.h"
#include "world/museum.h"

namespace museum {

class Engine {
 public:
  Engine() = default;

  bool Initialize(const char* title, std::uint32_t width, std::uint32_t height);

  // One frame: input -> simulation -> render.
  void Frame();

  bool quit_requested() const { return window_.quit_requested(); }
  bool ready() const { return renderer_.ready(); }
  bool running() const { return initialized_; }

  const world::Museum& museum() const { return museum_; }
  const player::Player& player() const { return player_; }
  const render::Renderer& renderer() const { return renderer_; }

 private:
  void HandleResize();

  world::Museum museum_;
  platform::Window window_;
  core::Clock clock_;
  player::Player player_;
  render::Renderer renderer_;
  bool initialized_ = false;
};

}  // namespace museum
