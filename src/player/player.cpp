#include "player/player.h"

#include <algorithm>
#include <cmath>

#include "world/config.h"

namespace museum::player {
namespace {

// Warp FX: FOV punch + subtle roll over 0.8s, matching the JS museum.
constexpr float kWarpDuration = 0.8f;
constexpr float kWarpFovPunch = 38.0f;
constexpr float kWarpRoll = 0.07f;

}  // namespace

Player::Player() { Respawn(); }

void Player::ApplyLook(float dx_pixels, float dy_pixels) {
  yaw_ -= dx_pixels * kMouseSensitivity;
  pitch_ -= dy_pixels * kMouseSensitivity;

  const float limit = math::Radians(kMaxPitchDegrees);
  pitch_ = math::Clamp(pitch_, -limit, limit);
}

void Player::Update(float dt, const MoveInput& move, const platform::InputState& input) {
  ApplyLook(input.look_yaw, input.look_pitch);

  // Godot's `direction = basis * input`, always normalized. Forward ignores
  // pitch so looking down does not slow horizontal movement.
  Vec3 direction{move.strafe, 0.0f, -move.forward};
  const float length = math::Length(direction);
  if (length > 0.0f) {
    direction = direction * (1.0f / length);

    // Rotate the local direction by the yaw rig.
    const float c = std::cos(yaw_);
    const float s = std::sin(yaw_);
    velocity_.x = (direction.x * c + direction.z * s) * kSpeed;
    velocity_.z = (-direction.x * s + direction.z * c) * kSpeed;
  } else {
    velocity_.x = 0.0f;
    velocity_.z = 0.0f;
  }

  // Gravity always accumulates; floor contact zeroes it.
  velocity_.y -= kGravity * dt;
  if (velocity_.y < -kTerminalVelocity) {
    velocity_.y = -kTerminalVelocity;
  }

  // Substep so no single slice exceeds the thinnest collider (0.15 m slabs and
  // walls cannot be tunnelled through at any speed).
  on_ground_ = false;
  if (world_ != nullptr) {
    const float speed = math::Length(velocity_);
    const float distance = speed * dt;
    int steps = static_cast<int>(std::ceil(distance / kMaxSubstepDistance));
    steps = std::min(kMaxSubsteps, std::max(1, steps));
    const float sub = dt / static_cast<float>(steps);

    const HalfExtents half = Half();
    for (int i = 0; i < steps; ++i) {
      const Vec3 delta{velocity_.x * sub, velocity_.y * sub, velocity_.z * sub};
      if (world_->Move(position_, half, delta)) {
        on_ground_ = true;
        if (velocity_.y < 0.0f) {
          velocity_.y = 0.0f;
        }
      }
    }
  } else {
    // No world bound yet: fall straight down onto the first floor.
    position_.y += velocity_.y * dt;
    const float ground = config::FloorTopY(0) + kCapsuleHeight / 2.0f;
    if (position_.y <= ground) {
      position_.y = ground;
      velocity_.y = 0.0f;
      on_ground_ = true;
    }
  }

  if (on_ground_) {
    velocity_.y = 0.0f;
    fall_timer_ = 0.0f;
  } else {
    fall_timer_ += dt;
    if (fall_timer_ > kFallRespawnSeconds || position_.y < kVoidY) {
      Respawn();
    }
  }

  if (warp_time_ >= 0.0f) {
    warp_time_ += dt;
    if (warp_time_ / kWarpDuration >= 1.0f) {
      warp_time_ = -1.0f;
    }
  }
}

void Player::StartWarp() { warp_time_ = 0.0f; }

void Player::TeleportTo(float x, float y, float z) {
  position_ = {x, y, z};
  velocity_ = {};
  fall_timer_ = 0.0f;
}

void Player::SetPose(float x, float y, float z, float yaw, float pitch) {
  TeleportTo(x, y, z);
  yaw_ = yaw;
  const float limit = math::Radians(kMaxPitchDegrees);
  pitch_ = math::Clamp(pitch, -limit, limit);
}

void Player::Respawn() {
  TeleportTo(kSpawnX, config::FloorTopY(0) + kCapsuleHeight / 2.0f, kSpawnZ);
  yaw_ = kSpawnYaw;
  pitch_ = kInitialPitch;
}

Vec3 Player::Forward() const {
  const float cp = std::cos(pitch_);
  return {-std::sin(yaw_) * cp, std::sin(pitch_), -std::cos(yaw_) * cp};
}

Vec3 Player::Right() const { return {std::cos(yaw_), 0.0f, -std::sin(yaw_)}; }

ViewMatrices Player::BuildViewMatrices(float aspect) const {
  // Yaw then pitch, applied as R = Ry * Rx to the camera's local -Z forward.
  const math::Mat4 rotation =
      math::Mat4::RotationAxis({0, 1, 0}, yaw_) *
      math::Mat4::RotationAxis({1, 0, 0}, pitch_);

  // The eye sits above the collision box centre.
  float eye_y = position_.y + kCameraEyeOffsetY;

  float fov = kFov;
  float roll = 0.0f;
  if (warp_time_ >= 0.0f) {
    const float t = math::Min(warp_time_ / kWarpDuration, 1.0f);
    const float s = std::sin(math::kPi * t);
    fov = kFov + kWarpFovPunch * s;
    roll = std::sin(t * math::kPi * 3.0f) * kWarpRoll * (1.0f - t);
  }

  const math::Mat4 camera_to_world =
      math::Mat4::Translation({position_.x, eye_y, position_.z}) *
      math::Mat4::RotationAxis({0, 0, 1}, roll) * rotation;

  ViewMatrices out;
  out.view = camera_to_world.Inverse();
  out.projection = math::Mat4::Perspective(math::Radians(fov), aspect, 0.05f, 4000.0f);
  out.view_projection = out.projection * out.view;
  out.camera_position = {position_.x, eye_y, position_.z};
  return out;
}

}  // namespace museum::player
