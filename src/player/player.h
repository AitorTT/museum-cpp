// The player: a camera on legs. Owns the yaw/pitch rig and produces the view
// and projection matrices the renderer uploads each frame.
//
// The values below are lifted from the JS museum's config.ts, which in turn
// ported them from Godot's character_body_3d.gd, so the desk-feel and the
// spawn point stay identical across all three museums.
#pragma once

#include "core/math.h"
#include "platform/window.h"

namespace museum::player {

using math::MoveInput;
using math::Vec3;

// player.tscn + character_body_3d.gd exports
inline constexpr float kSpeed = 7.0f;              // metres/second
inline constexpr float kGravity = 9.8f * 3.0f;     // the JS museum triples it
inline constexpr float kEyeHeight = 1.60862f * 1.1f;
inline constexpr float kCapsuleRadius = 0.5f * 1.1f;
inline constexpr float kCapsuleHeight = 2.0f * 1.1f;
inline constexpr float kMaxPitchDegrees = 45.0f;
inline constexpr float kMouseSensitivity = 0.005f;  // radians per pixel
inline constexpr float kInitialPitch = -0.281186f;  // the baked camera tilt

inline constexpr float kSpawnX = -3.1290083f;
inline constexpr float kSpawnZ = -8.900534f;
inline constexpr float kSpawnYaw = math::kPi / 2.0f;

struct ViewMatrices {
  math::Mat4 view;
  math::Mat4 projection;
  math::Mat4 view_projection;
  Vec3 camera_position;  // eye position, warp offset included
};

class Player {
 public:
  Player();

  // Applies one frame of look input (pixels), then integrates movement and
  // gravity. `move` comes from the merged keyboard/stick state.
  void Update(float dt, const MoveInput& move, const platform::InputState& input);

  // Bumps the eye up briefly; used when a teleporter fires.
  void StartWarp();

  bool warping() const { return warp_time_ > 0.0f; }

  const Vec3& position() const { return position_; }
  void set_position(const Vec3& position) { position_ = position; }

  float yaw() const { return yaw_; }
  float pitch() const { return pitch_; }

  float fov_degrees() const { return fov_degrees_; }

  ViewMatrices BuildViewMatrices(float aspect) const;

  // Camera-relative direction vectors, for future object interaction.
  Vec3 Forward() const;
  Vec3 Right() const;

 private:
  void ApplyLook(float dx_pixels, float dy_pixels);

  Vec3 position_;
  float yaw_ = kSpawnYaw;
  float pitch_ = kInitialPitch;
  float vertical_velocity_ = 0.0f;
  float fov_degrees_ = 50.0f;
  float warp_time_ = 0.0f;
};

}  // namespace museum::player
