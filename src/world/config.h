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

// Lighting.
//
// The JS museum approximated Godot's LightmapGI + ceiling spots by baking light
// into vertex colours, because it had no runtime lights at all (a mobile
// constraint). These are its bake constants, kept so the two builds agree, but
// here the same model is evaluated per-pixel with real shadows instead of being
// frozen into 5x4 vertex grids.
inline constexpr float kAmbientIntensity = 1.0f;
inline constexpr float kBakeEnergy = 48.0f;    // spot intensity for E/d^2
inline constexpr float kBakeMinDistance = 1.5f;
inline constexpr float kBakeDirectScale = 1.0f;
inline constexpr float kBakeConeInner = 0.75f;  // cos(angle): full inside
inline constexpr float kBakeConeOuter = 0.35f;  // cos(angle): zero outside
inline constexpr float kBakeBounceWall = 0.55f;
inline constexpr float kBakeBounceFloor = 0.55f;
inline constexpr float kBakeCeilBase = 0.3f;
inline constexpr float kBakeCeilGlow = 0.9f;
inline constexpr float kBakeCeilSigma2 = 26.0f;
inline constexpr float kBakeMin = 0.08f;
inline constexpr float kBakeMax = 1.8f;

/** Walkable surface height of a museum floor (floor slab top). */
inline constexpr float FloorTopY(int floor) {
  return static_cast<float>(floor) * kRoomHeight - kRoomHeight / 2.0f + kWallThickness;
}

}  // namespace museum::config
