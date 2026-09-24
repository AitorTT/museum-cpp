#include "world/museum.h"

#include <algorithm>

#include "core/math.h"
#include "player/player.h"
#include "world/config.h"
#include "world/layout.h"

namespace museum::world {
namespace {

// Appends one axis-aligned box as 24 vertices / 36 indices, with flat normals.
// Each box is emitted independently rather than merged with its neighbours: the
// JS museum merged only to reduce draw calls, and the C++ path will use
// instancing and a single buffer instead, so keeping them separate here loses
// nothing and keeps the light anchor per box exact.
void AppendBox(const SegmentBox& box, Mesh& mesh) {
  const float x0 = box.MinX();
  const float x1 = box.MaxX();
  const float y0 = box.MinY();
  const float y1 = box.MaxY();
  const float z0 = box.MinZ();
  const float z1 = box.MaxZ();

  // Floors get the floor material and per-metre texture tiling; walls get a
  // flat colour. Ceilings behave as walls.
  const bool is_floor_box = box.kind == SegmentKind::kFloor;
  const float floor_flag = is_floor_box ? 1.0f : 0.0f;
  const float uv_scale = is_floor_box ? 2.0f : 1.0f;

  // Six faces, each with its own normal and a 0..1 UV square.
  struct Face {
    float nx, ny, nz;
    float corners[4][3];
  };
  const Face faces[6] = {
      // +X
      {1.0f, 0.0f, 0.0f, {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}},
      // -X
      {-1.0f, 0.0f, 0.0f, {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}},
      // +Y
      {0.0f, 1.0f, 0.0f, {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}},
      // -Y
      {0.0f, -1.0f, 0.0f, {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}},
      // +Z
      {0.0f, 0.0f, 1.0f, {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}},
      // -Z
      {0.0f, 0.0f, -1.0f, {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}},
  };

  // UVs scaled so the texture tiles per world unit on the floor, matching the
  // JS museum's uv1_scale of 2.
  const float u_scale = uv_scale;

  for (const Face& face : faces) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());

    // Two of the box's dimensions drive the UV rectangle for this face.
    float u_extent = 1.0f;
    float v_extent = 1.0f;
    if (face.nx != 0.0f) {
      u_extent = box.sz;
      v_extent = box.sy;
    } else if (face.ny != 0.0f) {
      u_extent = box.sx;
      v_extent = box.sz;
    } else {
      u_extent = box.sx;
      v_extent = box.sy;
    }

    const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 4; ++i) {
      Vertex vertex;
      vertex.px = face.corners[i][0];
      vertex.py = face.corners[i][1];
      vertex.pz = face.corners[i][2];
      vertex.nx = face.nx;
      vertex.ny = face.ny;
      vertex.nz = face.nz;
      vertex.u = uvs[i][0] * u_extent * u_scale;
      vertex.v = uvs[i][1] * v_extent * u_scale;
      vertex.is_floor = floor_flag;
      mesh.vertices.push_back(vertex);
    }

    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
  }
}

}  // namespace

void Museum::Build() {
  const std::vector<FloorDef>& floors = MuseumLayout();

  float min_y = 1e30f;
  float max_y = -1e30f;

  for (const FloorDef& floor : floors) {
    for (const RoomDef& room : floor.rooms) {
      const RoomPlan plan = PlanRoom(room, floor.y);
      stats_.rooms++;

      for (const SegmentBox& box : plan.boxes) {
        AddSegment(box);
        min_y = math::Min(min_y, box.MinY());
        max_y = math::Max(max_y, box.MaxY());
      }

      if (plan.has_painting_spot) {
        painting_spots_.push_back(plan.painting_spot);
        stats_.painting_spots++;
      }
    }
  }

  AddCornerPatches();

  stats_.segments = static_cast<int>(segments_.size());
  stats_.colliders = static_cast<int>(collision_.boxes.size());
  stats_.min_y = min_y;
  stats_.max_y = max_y;
}

void Museum::AddSegment(const SegmentBox& box) {
  segments_.push_back(box);

  // Screenshots show the floor/ceiling slabs as well as the walls; all of them
  // double as colliders, which is what makes the world solid.
  const bool is_floor = box.kind == SegmentKind::kFloor;
  collision_.AddBox(box.cx, box.cy, box.cz, box.sx, box.sy, box.sz, is_floor);
}

void Museum::AddCornerPatches() {
  // Corner patches: diagonal seams between rooms (where a diagonal quadrant has
  // no room) let a 1.1m-wide player drop through the exact grid corner. A 2.2m
  // patch at each corner point that touches a room keeps support across the
  // seam; the patches never extend past the wall lines.
  constexpr float kGrid = 10.0f;
  const float patch_y = -config::kRoomHeight / 2.0f + config::kWallThickness / 2.0f;
  const float patch_size = player::kCapsuleRadius * 4.0f;

  const std::vector<FloorDef>& floors = MuseumLayout();

  for (const FloorDef& floor : floors) {
    // Collect the distinct corner points of every room on this floor.
    std::vector<std::pair<float, float>> corners;
    const auto add_corner = [&corners](float x, float z) {
      for (const auto& existing : corners) {
        if (existing.first == x && existing.second == z) {
          return;
        }
      }
      corners.emplace_back(x, z);
    };

    for (const RoomDef& room : floor.rooms) {
      for (float sx : {-5.0f, 5.0f}) {
        for (float sz : {-5.0f, 5.0f}) {
          add_corner(room.x + sx, room.z + sz);
        }
      }
    }

    for (const auto& corner : corners) {
      const float px = corner.first;
      const float pz = corner.second;

      const auto touches = [&floor, px, pz]() {
        for (const RoomDef& room : floor.rooms) {
          if (px >= room.x - 5.0f && px <= room.x + 5.0f && pz >= room.z - 5.0f &&
              pz <= room.z + 5.0f) {
            return true;
          }
        }
        return false;
      }();

      if (touches) {
        collision_.AddBox(px, floor.y + patch_y, pz, patch_size,
                          config::kWallThickness, patch_size, true);
      }
    }
  }

  (void)kGrid;
}

void Museum::EmitMesh(Mesh& out) const {
  // Walls and ceilings first.
  const std::uint32_t wall_begin = static_cast<std::uint32_t>(out.indices.size());
  for (const SegmentBox& box : segments_) {
    if (box.kind == SegmentKind::kFloor) {
      continue;
    }
    AppendBox(box, out);
  }
  (void)wall_begin;

  // Then floors, as one contiguous range for the floor material.
  out.floor_index_begin = static_cast<std::uint32_t>(out.indices.size());
  for (const SegmentBox& box : segments_) {
    if (box.kind != SegmentKind::kFloor) {
      continue;
    }
    AppendBox(box, out);
  }
  out.floor_index_count =
      static_cast<std::uint32_t>(out.indices.size()) - out.floor_index_begin;
}

}  // namespace museum::world
