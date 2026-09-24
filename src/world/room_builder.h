// Port of RoomBuilder.gd's wall segmentation + MuseumBuilder.gd's room planning,
// via the JS museum's RoomBuilder.ts.
//
// Every piece of the museum is an axis-aligned box. Each box doubles as a
// collider, which is what lets the whole collider set be built here rather than
// derived from the mesh afterwards.
#pragma once

#include <vector>

#include "world/layout.h"

namespace museum::world {

enum class SegmentKind {
  kWall,
  kFloor,
  kCeiling,
};

// An axis-aligned box, centred, with a ceiling light anchor used by the baked
// vertex lighting in Batch 4.
struct SegmentBox {
  float cx = 0.0f;
  float cy = 0.0f;
  float cz = 0.0f;
  float sx = 0.0f;
  float sy = 0.0f;
  float sz = 0.0f;
  SegmentKind kind = SegmentKind::kWall;

  // Ceiling light anchor (room centre, just under the ceiling).
  float lx = 0.0f;
  float ly = 0.0f;
  float lz = 0.0f;

  // Convenience for the renderer: the box's world-space bounds.
  float MinX() const { return cx - sx * 0.5f; }
  float MinY() const { return cy - sy * 0.5f; }
  float MinZ() const { return cz - sz * 0.5f; }
  float MaxX() const { return cx + sx * 0.5f; }
  float MaxY() const { return cy + sy * 0.5f; }
  float MaxZ() const { return cz + sz * 0.5f; }
};

struct PaintingSpot {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float ry = 0.0f;
};

struct RoomPlan {
  std::vector<SegmentBox> boxes;
  bool has_painting_spot = false;
  PaintingSpot painting_spot;
};

/** Plans one room: its wall/floor/ceiling segments and painting spot. */
RoomPlan PlanRoom(const RoomDef& room, float floor_y);

}  // namespace museum::world
