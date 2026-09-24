// The player: a camera on legs. Owns the yaw/pitch rig, integrates movement and
// gravity through the collision world, and produces the view and projection
// matrices the renderer uploads each frame.
//
// The values below are lifted from the JS museum's config.ts, which in turn
// ported them from Godot's character_body_3d.gd, so the feel and the spawn point
// stay identical across all three museums.
#pragma once

#include "core/math.h"
#include "platform/window.h"
#include "world/collision.h"

namespace museum::player {

using math::MoveInput;
using math::Vec3;
using world::CollisionWorld;
using world::HalfExtents;

// player.tscn + character_body_3d.gd exports
inline constexpr float kSpeed = 7.0f;               // metres/second
inline constexpr float kGravity = 9.8f * 3.0f;      // the JS museum triples it
inline constexpr float kMaxPitchDegrees = 45.0f;
inline constexpr float kMouseSensitivity = 0.005f;  // radians per pixel
inline constexpr float kInitialPitch = -0.281186f;  // the baked camera tilt
inline constexpr float kFov = 50.0f;

// player.tscn geometry (default capsule r=0.5 h=2.0, node scaled 1.1x)
inline constexpr float kPlayerScale = 1.1f;
inline constexpr float kCapsuleRadius = 0.5f * kPlayerScale;
inline constexpr float kCapsuleHeight = 2.0f * kPlayerScale;
inline constexpr float kCameraLocalY = 1.60862f * kPlayerScale;
inline constexpr float kCameraEyeOffsetY = kCameraLocalY - kCapsuleHeight / 2.0f;

inline constexpr float kSpawnX = -3.1290083f;
inline constexpr float kSpawnZ = -8.900534f;
inline constexpr float kSpawnYaw = math::kPi / 2.0f;

// Physics tuning, ported from Player.ts.
inline constexpr float kFallRespawnSeconds = 2.0f;
inline constexpr float kMaxSubstepDistance = 0.08f;
inline constexpr int kMaxSubsteps = 24;
inline constexpr float kTerminalVelocity = 25.0f;
inline constexpr float kVoidY = -40.0f;

struct ViewMatrices {
  math::Mat4 view;
  math::Mat4 projection;
  math::Mat4 view_projection;
  Vec3 camera_position;  // eye position, warp offset included
};

class Player {
 public:
  Player();

  // Binds the collision world. Must be called before the first Update.
  void SetWorld(const CollisionWorld* world) { world_ = world; }

  // Applies one frame of look input (pixels), then integrates movement and
  // gravity.
  void Update(float dt, const MoveInput& move, const platform::InputState& input);

  // Bumps the eye up briefly; used when a teleporter fires.
  void StartWarp();

  // Instant repositioning (teleporter pads); keeps the current view angles.
  void TeleportTo(float x, float y, float z);

  // Returns to the spawn point.
  void Respawn();

  bool on_ground() const { return on_ground_; }
  bool warping() const { return warp_time_ >= 0.0f; }

  const Vec3& position() const { return position_; }
  Vec3& mutable_position() { return position_; }

  float yaw() const { return yaw_; }
  float pitch() const { return pitch_; }

  ViewMatrices BuildViewMatrices(float aspect) const;

  Vec3 Forward() const;
  Vec3 Right() const;

  static HalfExtents Half() {
    return HalfExtents{kCapsuleRadius, kCapsuleHeight / 2.0f, kCapsuleRadius};
  }

 private:
  void ApplyLook(float dx_pixels, float dy_pixels);

  const CollisionWorld* world_ = nullptr;

  // `position_` is the collision box centre, as in the JS museum (Godot's eye
  // offset was measured above the feet; here the eye is derived from it).
  Vec3 position_;
  Vec3 velocity_;
  float yaw_ = kSpawnYaw;
  float pitch_ = kInitialPitch;
  float fall_timer_ = 0.0f;
  float warp_time_ = -1.0f;
  bool on_ground_ = false;
};

}  // namespace museum::player
