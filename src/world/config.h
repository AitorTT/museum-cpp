// Port of the JS museum's config.ts (which ported Godot's scene overrides).
// Kept in one place so the three museums stay numerically identical.
#pragma once

namespace museum::config {

// Room dimensions (RoomGenerator.tscn overrides)
inline constexpr float kRoomWidth = 10.0f;
inline constexpr float kRoomHeight = 6.0f;
inline constexpr float kRoomDepth = 10.0f;
inline constexpr float kWallThickness = 0.15f;

// Doors (door_width is set to 2.0 at build time in MuseumBuilder)
inline constexpr float kDoorWidth = 2.0f;
inline constexpr float kDoorHeight = 3.0f;

// Windows (width 12 -> clamped per wall, height 5, bottom 2)
inline constexpr float kWindowWidth = 12.0f;
inline constexpr float kWindowHeight = 5.0f;
inline constexpr float kWindowBottom = 2.0f;

// Paintings (used from Batch 5; kept here so the layout maths is complete)
inline constexpr float kPaintingHeight = 2.0f;
inline constexpr float kPaintingScale = 2.0f;
inline constexpr float kFramePadding = 0.05f;
inline constexpr float kFrameDepth = 0.08f;

/** Walkable surface height of a museum floor (floor slab top). */
inline constexpr float FloorTopY(int floor) {
  return static_cast<float>(floor) * kRoomHeight - kRoomHeight / 2.0f + kWallThickness;
}

}  // namespace museum::config
