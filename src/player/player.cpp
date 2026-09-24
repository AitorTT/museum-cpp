#include "player/player.h"

#include <algorithm>
#include <cmath>

namespace museum::player {
namespace {

// Total warp punch: rise and settle. Kept short so it reads as a whoosh
// rather than a slow levitation.
constexpr float kWarpDuration = 0.6f;
constexpr float kWarpRise = 0.35f;

}  // namespace

Player::Player() {
  position_ = {kSpawnX, kEyeHeight, kSpawnZ};
  pitch_ = kInitialPitch;
}

void Player::ApplyLook(float dx_pixels, float dy_pixels) {
  yaw_ -= dx_pixels * kMouseSensitivity;
  pitch_ -= dy_pixels * kMouseSensitivity;

  const float limit = math::Radians(kMaxPitchDegrees);
  pitch_ = math::Clamp(pitch_, -limit, limit);
}

void Player::Update(float dt, const MoveInput& move, const platform::InputState& input) {
  ApplyLook(input.look_yaw, input.look_pitch);

  // Camera-relative basis. Forward ignores pitch so looking down does not slow
  // horizontal movement (the JS museum does the same).
  Vec3 forward{-std::sin(yaw_), 0.0f, -std::cos(yaw_)};
  Vec3 right{std::cos(yaw_), 0.0f, -std::sin(yaw_)};

  Vec3 wish = forward * move.forward + right * move.strafe;
  const float wish_length = math::Length(wish);
  if (wish_length > 1.0f) {
    wish = wish * (1.0f / wish_length);
  }

  position_.x += wish.x * kSpeed * dt;
  position_.z += wish.z * kSpeed * dt;

  // Gravity, until collision lands on top of a floor. Batch 2 replaces the
  // bare eye-height clamp with the real AABB collider from the museum grid.
  vertical_velocity_ -= kGravity * dt;
  position_.y += vertical_velocity_ * dt;

  const float ground = kEyeHeight;
  if (position_.y <= ground) {
    position_.y = ground;
    vertical_velocity_ = 0.0f;
  }

  if (warp_time_ > 0.0f) {
    warp_time_ = std::max(0.0f, warp_time_ - dt);
  }
}

void Player::StartWarp() { warp_time_ = kWarpDuration; }

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

  // Eye position, including the warp rise.
  float eye_y = position_.y;
  if (warp_time_ > 0.0f) {
    const float t = warp_time_ / kWarpDuration;
    eye_y += std::sin(t * math::kPi) * kWarpRise;
  }

  const math::Mat4 camera_to_world =
      math::Mat4::Translation({position_.x, eye_y, position_.z}) * rotation;

  ViewMatrices out;
  out.view = camera_to_world.Inverse();
  out.projection = math::Mat4::Perspective(math::Radians(fov_degrees_), aspect,
                                          0.05f, 1500.0f);
  out.view_projection = out.projection * out.view;
  out.camera_position = {position_.x, eye_y, position_.z};
  return out;
}

}  // namespace museum::player
