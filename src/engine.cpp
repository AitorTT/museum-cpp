#include "engine.h"

#include <cstdio>

namespace museum {

bool Engine::Initialize(const char* title, std::uint32_t width, std::uint32_t height) {
  museum_.Build();
  const world::MuseumStats& stats = museum_.stats();
  std::printf(
      "Museum: %d rooms, %d segments, %d colliders, %d painting spots, "
      "y %.1f..%.1f\n",
      stats.rooms, stats.segments, stats.colliders, stats.painting_spots,
      stats.min_y, stats.max_y);

  world::Mesh mesh;
  museum_.EmitMesh(mesh);
  std::printf("Museum: mesh %zu vertices, %zu indices\n", mesh.vertices.size(),
              mesh.indices.size());

  const world::SkyMesh sky = world::BuildSkyDome();

  if (!window_.Create(title, width, height)) {
    return false;
  }

  player_.SetWorld(&museum_.collision());

  if (!renderer_.Initialize(&window_, mesh, sky, museum_.lights(), kAssetsDir)) {
    return false;
  }

  initialized_ = true;
  return true;
}

void Engine::HandleResize() {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  if (window_.ConsumeResize(&width, &height)) {
    renderer_.Resize(width, height);
  }
}

void Engine::Frame() {
  if (!initialized_) {
    return;
  }

  window_.PollEvents();
  HandleResize();

  const float dt = clock_.Tick();

  math::MoveInput move;
  move.forward = window_.input().move_forward;
  move.strafe = window_.input().move_strafe;

  // A held look stick is a rotation rate, but the look path is expressed in
  // pixel deltas (the same units a mouse reports), so convert using dt. At full
  // deflection this turns at kLookStickSpeed radians per second.
  //
  // The sign is worth spelling out, because it is easy to get backwards twice.
  // ApplyLook does `yaw_ -= dx`, so the value written here is subtracted, not
  // added. The stick's x is screen-right positive, and a right push must turn
  // right, which is yaw decreasing. yaw_ -= look_yaw, so for yaw to fall on a
  // positive x, look_yaw must be positive: hence `+= x`, not `+= -x`.
  platform::InputState& input = window_.mutable_input();
  if (input.look_stick_active) {
    const float pixels = player::kLookStickSpeed * dt;
    input.look_yaw += input.look_stick_x * pixels;
    // y is screen-up positive in the stick but ApplyLook wants screen-down
    // positive, and it subtracts, so this one does negate.
    input.look_pitch += -input.look_stick_y * pixels;
  }

  player_.Update(dt, move, window_.input());

  const float aspect = window_.height() > 0
                           ? static_cast<float>(window_.width()) /
                                 static_cast<float>(window_.height())
                           : 1.0f;
  renderer_.RenderFrame(player_.BuildViewMatrices(aspect));

  window_.EndFrame();
}

}  // namespace museum
