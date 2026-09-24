// The museum's light sources: one ceiling spot per room, exactly where
// RoomBuilder places the fixture (room centre, just under the ceiling).
//
// The JS museum has no lights at all: it folds this model into vertex colours
// at build time and ships a fully baked, static scene. Doing it per-pixel here
// is the point of the C++ rewrite, and it also means the light can finally
// throw shadows, which a vertex bake cannot do without a separate offline pass.
#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"

namespace museum::world {

using math::Vec3;

struct SpotLight {
  Vec3 position;    // the fixture, at the ceiling
  Vec3 direction;   // pointing straight down, as in RoomBuilder
  float energy = 0.0f;
  float range = 0.0f;
  float cone_inner = 0.0f;  // cos(angle) of the inner cone
  float cone_outer = 0.0f;  // cos(angle) of the outer cone
};

// How many lights a single fragment may be lit by. Rooms are 10u apart and the
// spot range is 15u, so a fragment can see its own room's fixture plus a
// neighbour's through a doorway; 4 is comfortable and keeps the uniform small.
inline constexpr int kMaxLightsPerFragment = 4;

}  // namespace museum::world
