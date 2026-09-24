#include "world/room_builder.h"

#include "core/math.h"
#include "world/config.h"

namespace museum::world {
namespace {

constexpr float kHalfWidth = config::kRoomWidth / 2.0f;
constexpr float kHalfDepth = config::kRoomDepth / 2.0f;
constexpr float kHalfHeight = config::kRoomHeight / 2.0f;

// Painting placement offset (RoomBuilder._add_room_features)
constexpr float kFaceInset = config::kWallThickness + 0.02f;

// Accumulates segments into a plan, translating room-local coordinates to world
// space and stamping every box with the room's light anchor.
struct Planner {
  RoomPlan plan;
  const RoomDef* room = nullptr;
  float floor_y = 0.0f;
  float anchor_x = 0.0f;
  float anchor_y = 0.0f;
  float anchor_z = 0.0f;

  void Add(float lx, float ly, float lz, float sx, float sy, float sz,
           SegmentKind kind = SegmentKind::kWall) {
    SegmentBox box;
    box.cx = lx + room->x;
    box.cy = ly + floor_y;
    box.cz = lz + room->z;
    box.sx = sx;
    box.sy = sy;
    box.sz = sz;
    box.kind = kind;
    box.lx = anchor_x;
    box.ly = anchor_y;
    box.lz = anchor_z;
    plan.boxes.push_back(box);
  }

  // Port of RoomBuilder._add_z_wall: front/back walls, segments span X and the
  // thickness runs along Z.
  void ZWall(float lx, float ly, float lz, float fw, bool has_door,
             bool has_window) {
    const float fh = config::kRoomHeight;
    const float t = config::kWallThickness;

    if (has_door) {
      const float dw = math::Min(config::kDoorWidth, fw - 0.2f);
      const float dh = math::Min(config::kDoorHeight, fh - 0.1f);
      const float lw = (fw - dw) * 0.5f;
      const float th = fh - dh;
      if (lw > 0.01f) {
        Add(lx - (fw - lw) * 0.5f, ly, lz, lw, fh, t);
        Add(lx + (fw - lw) * 0.5f, ly, lz, lw, fh, t);
      }
      if (th > 0.01f) {
        Add(lx, ly + (fh - th) * 0.5f, lz, dw, th, t);
      }
      return;  // door filler is invisible and non-colliding in Godot: skipped
    }

    if (has_window) {
      const float ww = math::Min(config::kWindowWidth, fw - 0.2f);
      const float wh = math::Min(config::kWindowHeight, fh - 0.1f);
      const float wcy = -fh * 0.5f + config::kWindowBottom + wh * 0.5f;
      const float lw = (fw - ww) * 0.5f;
      const float th = fh * 0.5f - (wcy + wh * 0.5f);
      const float bh = wcy - wh * 0.5f - (-fh * 0.5f);
      if (lw > 0.01f) {
        Add(lx - (fw - lw) * 0.5f, ly, lz, lw, fh, t);
        Add(lx + (fw - lw) * 0.5f, ly, lz, lw, fh, t);
      }
      if (th > 0.01f) {
        const float cy = (fh * 0.5f + wcy + wh * 0.5f) * 0.5f;
        Add(lx, ly + cy, lz, ww, th, t);
      }
      if (bh > 0.01f) {
        const float cy = (-fh * 0.5f + wcy - wh * 0.5f) * 0.5f;
        Add(lx, ly + cy, lz, ww, bh, t);
      }
      return;
    }

    Add(lx, ly, lz, fw, fh, t);
  }

  // Port of RoomBuilder._add_x_wall: left/right walls, the thickness runs along
  // X and segments span Z.
  void XWall(float lx, float ly, float lz, float fw, bool has_door,
             bool has_window) {
    const float fh = config::kRoomHeight;
    const float t = config::kWallThickness;

    if (has_door) {
      const float dw = math::Min(config::kDoorWidth, fw - 0.2f);
      const float dh = math::Min(config::kDoorHeight, fh - 0.1f);
      const float lw = (fw - dw) * 0.5f;
      const float th = fh - dh;
      if (lw > 0.01f) {
        Add(lx, ly, lz - (fw - lw) * 0.5f, t, fh, lw);
        Add(lx, ly, lz + (fw - lw) * 0.5f, t, fh, lw);
      }
      if (th > 0.01f) {
        Add(lx, ly + (fh - th) * 0.5f, lz, t, th, dw);
      }
      return;
    }

    if (has_window) {
      const float ww = math::Min(config::kWindowWidth, fw - 0.2f);
      const float wh = math::Min(config::kWindowHeight, fh - 0.1f);
      const float wcy = -fh * 0.5f + config::kWindowBottom + wh * 0.5f;
      const float lw = (fw - ww) * 0.5f;
      const float th = fh * 0.5f - (wcy + wh * 0.5f);
      const float bh = wcy - wh * 0.5f - (-fh * 0.5f);
      if (lw > 0.01f) {
        Add(lx, ly, lz - (fw - lw) * 0.5f, t, fh, lw);
        Add(lx, ly, lz + (fw - lw) * 0.5f, t, fh, lw);
      }
      if (th > 0.01f) {
        const float cy = (fh * 0.5f + wcy + wh * 0.5f) * 0.5f;
        Add(lx, ly + cy, lz, t, th, ww);
      }
      if (bh > 0.01f) {
        const float cy = (-fh * 0.5f + wcy - wh * 0.5f) * 0.5f;
        Add(lx, ly + cy, lz, t, bh, ww);
      }
      return;
    }

    Add(lx, ly, lz, t, fh, fw);
  }
};

}  // namespace

RoomPlan PlanRoom(const RoomDef& room, float floor_y) {
  Planner planner;
  planner.room = &room;
  planner.floor_y = floor_y;

  // RoomBuilder._add_light position: (0, hh - t - 0.05, 0) local.
  planner.anchor_x = room.x;
  planner.anchor_y = floor_y + kHalfHeight - config::kWallThickness - 0.05f;
  planner.anchor_z = room.z;

  const bool door_xp = HasDoor(room, kDoorXp);
  const bool door_xn = HasDoor(room, kDoorXn);
  const bool door_zp = HasDoor(room, kDoorZp);
  const bool door_zn = HasDoor(room, kDoorZn);

  // MuseumBuilder._room: window + painting assignment on door-free walls.
  // Note the JS source has a precedence quirk here: `doorXP ? 1 : 0 + ...`
  // binds as `doorXP ? 1 : (0 + ...)`, so a room with an xp door always counts
  // as exactly one door regardless of the others. Reproduced deliberately so
  // painting placement matches the JS museum, which matched Godot.
  int door_count = (door_xp ? 1 : 0 + (door_xn ? 1 : 0) + (door_zp ? 1 : 0) +
                                 (door_zn ? 1 : 0));
  const int paintings = door_count < 3 ? 1 : 0;

  const bool is_door[4] = {door_xp, door_xn, door_zp, door_zn};
  const char* wall_names[4] = {"xp", "xn", "zp", "zn"};

  // The order-free walls, in xp, xn, zp, zn order.
  int free_walls[4];
  int free_count = 0;
  for (int i = 0; i < 4; ++i) {
    if (!is_door[i]) {
      free_walls[free_count++] = i;
    }
  }

  // window_walls = free[0 .. free.length - paintings)
  bool is_window_wall[4] = {false, false, false, false};
  const int window_count = free_count - paintings > 0 ? free_count - paintings : 0;
  for (int i = 0; i < window_count; ++i) {
    is_window_wall[free_walls[i]] = true;
  }

  // painting_wall = free[free.length - paintings]
  int painting_wall = -1;
  if (paintings > 0 && free_count > 0) {
    painting_wall = free_walls[free_count - paintings];
  }

  // Walls (RoomBuilder._build_room_contents)
  planner.ZWall(0.0f, 0.0f, -kHalfDepth + config::kWallThickness * 0.5f,
                config::kRoomWidth + config::kWallThickness * 2.0f, door_zn,
                is_window_wall[3]);
  planner.ZWall(0.0f, 0.0f, kHalfDepth - config::kWallThickness * 0.5f,
                config::kRoomWidth + config::kWallThickness * 2.0f, door_zp,
                is_window_wall[2]);
  planner.XWall(-kHalfWidth + config::kWallThickness * 0.5f, 0.0f, 0.0f,
                config::kRoomDepth, door_xn, is_window_wall[1]);
  planner.XWall(kHalfWidth - config::kWallThickness * 0.5f, 0.0f, 0.0f,
                config::kRoomDepth, door_xp, is_window_wall[0]);

  // Floor + ceiling
  planner.Add(0.0f, -kHalfHeight + config::kWallThickness * 0.5f, 0.0f,
              config::kRoomWidth, config::kWallThickness, config::kRoomDepth,
              SegmentKind::kFloor);
  planner.Add(0.0f, kHalfHeight - config::kWallThickness * 0.5f, 0.0f,
              config::kRoomWidth, config::kWallThickness, config::kRoomDepth,
              SegmentKind::kCeiling);

  // Painting spot (RoomBuilder._add_room_features; offsets h/v are 0 in the
  // museum build)
  const float py = -kHalfHeight + config::kPaintingHeight;
  if (painting_wall >= 0) {
    PaintingSpot spot;
    spot.y = floor_y + py;
    if (painting_wall == 0) {  // xp
      spot.x = room.x + kHalfWidth - kFaceInset;
      spot.z = room.z;
      spot.ry = -math::kPi / 2.0f;
    } else if (painting_wall == 1) {  // xn
      spot.x = room.x - kHalfWidth + kFaceInset;
      spot.z = room.z;
      spot.ry = math::kPi / 2.0f;
    } else if (painting_wall == 2) {  // zp
      spot.x = room.x;
      spot.z = room.z + kHalfDepth - kFaceInset;
      spot.ry = math::kPi;
    } else {  // zn
      spot.x = room.x;
      spot.z = room.z - kHalfDepth + kFaceInset;
      spot.ry = 0.0f;
    }
    planner.plan.has_painting_spot = true;
    planner.plan.painting_spot = spot;
  }

  (void)wall_names;
  return planner.plan;
}

}  // namespace museum::world
