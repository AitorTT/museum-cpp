#include "world/museum.h"

#include <algorithm>

#include "core/math.h"
#include "player/player.h"
#include "world/config.h"
#include "world/layout.h"
#include "world/lights.h"

namespace museum::world {
namespace {

// Spot range, from the JS museum's note on the Godot scene overrides
// ("energy 8, range 15, 60deg cone"). The bake used energy 48 with an E/d^2
// model, and it drives the same equation here.
constexpr float kLightRange = 15.0f;

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
  const float material = is_floor_box ? kMaterialFloor : kMaterialWall;
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
      vertex.material = material;
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

// Appends one axis-aligned quad in the painting's local frame, rotated by `ry`
// and translated to the spot. Winding faces local +Z, which Painting.ts
// documents as the direction that faces into the room.
void AppendLocalQuad(const PaintingSpot& spot, float local_x0, float local_y0,
                     float local_x1, float local_y1, float local_z,
                     const float uvs[4][2], float material, float nx, float ny,
                     float nz, Mesh& mesh) {
  const float c = std::cos(spot.ry);
  const float s = std::sin(spot.ry);

  // Rotate about Y: (x, z) -> (x*c + z*s, -x*s + z*c), matching the engine's
  // yaw convention so a painting faces the same way the JS museum's does.
  const auto place = [&](float lx, float ly) {
    const float rx = lx * c + local_z * s;
    const float rz = -lx * s + local_z * c;
    return math::Vec3{spot.x + rx, spot.y + ly, spot.z + rz};
  };

  const math::Vec3 corners[4] = {
      place(local_x0, local_y0), place(local_x1, local_y0),
      place(local_x1, local_y1), place(local_x0, local_y1)};

  // The normal rotates with the quad.
  const float rnx = nx * c + nz * s;
  const float rnz = -nx * s + nz * c;

  const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
  for (int i = 0; i < 4; ++i) {
    Vertex vertex;
    vertex.px = corners[i].x;
    vertex.py = corners[i].y;
    vertex.pz = corners[i].z;
    vertex.nx = rnx;
    vertex.ny = ny;
    vertex.nz = rnz;
    vertex.u = uvs[i][0];
    vertex.v = uvs[i][1];
    vertex.material = material;
    mesh.vertices.push_back(vertex);
  }
  mesh.indices.push_back(base + 0);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
  mesh.indices.push_back(base + 0);
  mesh.indices.push_back(base + 2);
  mesh.indices.push_back(base + 3);
}

// One painting: four frame bars plus the canvas, from Painting.ts. The frame is
// a lit surface in the wall material (a dark grey box), the canvas an unshaded
// atlas sample floating just proud of it.
void AppendPainting(const PaintingSpot& spot, const Museum::PaintingUv& uv,
                    Mesh& mesh) {
  const float s = museum::config::kPaintingScale;
  const float fp = 0.05f;  // FRAME_PADDING
  const float fd = 0.08f;  // FRAME_DEPTH
  const float aspect = uv.aspect > 0.0f ? uv.aspect : 1.0f;

  const float fw = (aspect + fp * 2.0f) * s;
  const float fh = (1.0f + fp * 2.0f) * s;
  const float bar_t = fp * s;
  const float bar_d = fd * s;
  const float half_w = fw * 0.5f - bar_t * 0.5f;
  const float half_h = fh * 0.5f - bar_t * 0.5f;

  // Frame bars sit centred on the mounting plane; the canvas is pushed a
  // little into the room so it is never z-fighting with the frame's front face.
  const float frame_z = 0.0f;
  const float canvas_z = 0.0625f * s;

  const float flat[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

  // Top and bottom bars (span the full width), then left and right (full height).
  AppendLocalQuad(spot, -fw * 0.5f, half_h - bar_t * 0.5f, fw * 0.5f,
                  half_h + bar_t * 0.5f, frame_z, flat, kMaterialFrame, 0, 0, 1,
                  mesh);
  AppendLocalQuad(spot, -fw * 0.5f, -half_h - bar_t * 0.5f, fw * 0.5f,
                  -half_h + bar_t * 0.5f, frame_z, flat, kMaterialFrame, 0, 0, 1,
                  mesh);
  AppendLocalQuad(spot, -half_w - bar_t * 0.5f, -fh * 0.5f,
                  -half_w + bar_t * 0.5f, fh * 0.5f, frame_z, flat,
                  kMaterialFrame, 0, 0, 1, mesh);
  AppendLocalQuad(spot, half_w - bar_t * 0.5f, -fh * 0.5f,
                  half_w + bar_t * 0.5f, fh * 0.5f, frame_z, flat,
                  kMaterialFrame, 0, 0, 1, mesh);

  // The canvas: aspect*s wide and s tall, UVs straight into its atlas cell.
  const float cw = aspect * s;
  const float ch = 1.0f * s;
  const float canvas_uvs[4][2] = {{uv.u0, uv.v1},
                                  {uv.u1, uv.v1},
                                  {uv.u1, uv.v0},
                                  {uv.u0, uv.v0}};
  AppendLocalQuad(spot, -cw * 0.5f, -ch * 0.5f, cw * 0.5f, ch * 0.5f,
                  canvas_z, canvas_uvs, kMaterialPainting, 0, 0, 1, mesh);
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

      // One ceiling spot per room, at the fixture position RoomBuilder used as
      // this room's light anchor.
      if (!plan.boxes.empty()) {
        SpotLight light;
        light.position = {plan.boxes.front().lx, plan.boxes.front().ly,
                          plan.boxes.front().lz};
        // Straight down, as in RoomBuilder._add_light.
        light.direction = {0.0f, -1.0f, 0.0f};
        light.energy = museum::config::kBakeEnergy;
        light.range = kLightRange;
        light.cone_inner = museum::config::kBakeConeInner;
        light.cone_outer = museum::config::kBakeConeOuter;
        lights_.push_back(light);
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

void Museum::EmitMesh(Mesh& out, const std::vector<PaintingUv>& painting_uvs) const {
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

  // Finally the paintings: a dark frame plus an unshaded canvas. The JS museum
  // assigns paintings to the first `painting_uvs.size()` eligible spots, in
  // room order, and leaves the rest bare -- the same rule is in RoomBuilder.
  out.painting_index_begin = static_cast<std::uint32_t>(out.indices.size());
  const std::size_t painting_count =
      std::min(painting_spots_.size(), painting_uvs.size());
  for (std::size_t i = 0; i < painting_count; ++i) {
    AppendPainting(painting_spots_[i], painting_uvs[i], out);
  }
  out.painting_index_count =
      static_cast<std::uint32_t>(out.indices.size()) - out.painting_index_begin;
}

}  // namespace museum::world
