#include "world/layout.h"

#include <vector>

namespace museum::world {
namespace {

RoomDef R(float x, float z, int bits) { return RoomDef{x, z, bits}; }

std::vector<FloorDef> BuildLayout() {
  std::vector<FloorDef> floors;

  // --- Floor 1 ---
  {
    FloorDef floor;
    floor.name = "1stFloor";
    floor.y = 0.0f;
    floor.rooms = {
        R(-10, 20, 8),    // (0,1) zn
        R(-20, 10, 1),    // (1,0) xp
        R(-10, 10, 15),   // (1,1) all 4
        R(0, 10, 2),      // (1,2) xn
        R(-10, 0, 12),    // (2,1) zp+zn
        R(-10, -10, 13),  // (3,1) xp+zp+zn
        R(0, -10, 11),    // (3,2) xp+xn+zn
        R(10, -10, 3),    // (3,3) xp+xn
        R(20, -10, 11),   // (3,4) xp+xn+zn (xp doorway added for the elevator)
        R(-10, -20, 5),   // (4,1) xp+zp
        R(0, -20, 14),    // (4,2) xn+zp+zn
        R(20, -20, 12),   // (4,4) zp+zn
        R(0, -30, 5),     // (5,2) xp+zp
        R(10, -30, 3),    // (5,3) xp+xn
        R(20, -30, 6),    // (5,4) xn+zp
    };
    floors.push_back(floor);
  }

  // --- Floor 2 ---
  {
    FloorDef floor;
    floor.name = "2ndFloor";
    floor.y = 6.0f;
    floor.rooms = {
        R(-10, 20, 8),    // (0,1) zn
        R(-10, 10, 12),   // (1,1) zp+zn
        R(-10, 0, 12),    // (2,1) zp+zn
        R(-10, -10, 12),  // (3,1) zp+zn
        R(0, -10, 11),    // (3,2) xp+xn+zn (fills the original layout's floor hole)
        R(10, -10, 9),    // (3,3) xp+zn
        R(20, -10, 3),    // (3,4) xp+xn (xp doorway added for the elevator)
        R(-10, -20, 5),   // (4,1) xp+zp
        R(0, -20, 3),     // (4,2) xp+xn
        R(10, -20, 14),   // (4,3) xn+zp+zn
        R(10, -30, 5),    // (5,3) xp+zp
        R(20, -30, 2),    // (5,4) xn
    };
    floors.push_back(floor);
  }

  // --- Floor 3 ---
  {
    FloorDef floor;
    floor.name = "3rdFloor";
    floor.y = 12.0f;
    floor.rooms = {
        R(-10, 20, 8),    // (0,1) zn
        R(-10, 10, 12),   // (1,1) zp+zn
        R(-10, 0, 13),    // (2,1) xp+zp+zn
        R(0, 0, 3),       // (2,2) xp+xn
        R(10, 0, 2),      // (2,3) xn
        R(-10, -10, 12),  // (3,1) zp+zn
        R(-20, -20, 1),   // (4,0) xp
        R(-10, -20, 15),  // (4,1) xp+xn+zp+zn
        R(0, -20, 3),     // (4,2) xp+xn
        R(10, -20, 2),    // (4,3) xn
        R(-10, -30, 4),   // (5,1) zp
    };
    floors.push_back(floor);
  }

  // --- Floor 4 ---
  {
    FloorDef floor;
    floor.name = "4thFloor";
    floor.y = 18.0f;
    floor.rooms = {
        R(-10, 20, 8),    // (0,1) zn
        R(-10, 10, 12),   // (1,1) zp+zn
        R(-10, 0, 12),    // (2,1) zp+zn
        R(-10, -10, 12),  // (3,1) zp+zn
        R(-10, -20, 4),   // (4,1) zp
    };
    floors.push_back(floor);
  }

  return floors;
}

// Godot's grid has exterior doors that open onto the void (walking through them
// drops the player out of the world). Seal them: a door whose neighbour room
// does not exist on the same floor becomes a window wall instead. The elevator
// doorways on room (20,-10) are exempt, since their "neighbour" is the shaft.
void SealVoidDoors(std::vector<FloorDef>& floors) {
  constexpr float kGrid = 10.0f;

  for (auto& floor : floors) {
    const auto has = [&floor](float x, float z) {
      for (const RoomDef& room : floor.rooms) {
        if (room.x == x && room.z == z) {
          return true;
        }
      }
      return false;
    };

    for (RoomDef& room : floor.rooms) {
      if (HasDoor(room, kDoorXp) && !has(room.x + kGrid, room.z)) room.bits &= ~kDoorXp;
      if (HasDoor(room, kDoorXn) && !has(room.x - kGrid, room.z)) room.bits &= ~kDoorXn;
      if (HasDoor(room, kDoorZp) && !has(room.x, room.z + kGrid)) room.bits &= ~kDoorZp;
      if (HasDoor(room, kDoorZn) && !has(room.x, room.z - kGrid)) room.bits &= ~kDoorZn;
    }
  }

  // Restore the two elevator doorways.
  for (int floor_index : {0, 1}) {
    for (RoomDef& room : floors[floor_index].rooms) {
      if (room.x == 20.0f && room.z == -10.0f) {
        room.bits |= kDoorXp;
      }
    }
  }
}

}  // namespace

const std::vector<FloorDef>& MuseumLayout() {
  static const std::vector<FloorDef> floors = [] {
    std::vector<FloorDef> built = BuildLayout();
    SealVoidDoors(built);
    return built;
  }();
  return floors;
}

}  // namespace museum::world
