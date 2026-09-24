// Port of MuseumBuilder.gd's build() grid, via the JS museum's layout.ts.
//
// Door bits: xp=1, xn=2, zp=4, zn=8. Coordinates are room centres on a 10u grid.
#pragma once

#include <vector>

#include "world/config.h"

namespace museum::world {

enum DoorBit : int {
  kDoorXp = 1,
  kDoorXn = 2,
  kDoorZp = 4,
  kDoorZn = 8,
};

struct RoomDef {
  float x = 0.0f;
  float z = 0.0f;
  int bits = 0;
};

struct FloorDef {
  const char* name;
  float y;
  std::vector<RoomDef> rooms;
};

// The four floors, built once at startup and returned by reference.
const std::vector<FloorDef>& MuseumLayout();

/** Does this room have the given door bit set? */
inline bool HasDoor(const RoomDef& room, int bit) { return (room.bits & bit) != 0; }

}  // namespace museum::world
