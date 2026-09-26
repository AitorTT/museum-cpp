// Builds the whole museum: every room's segments, plus the corner patches that
// close the diagonal seams between rooms.
//
// The JS museum bakes light into vertex colours here. Batch 4 replaces that
// with real lighting, so right now the boxes only carry their geometry and the
// ceiling light anchor that the baker will use later.
#pragma once

#include <cstdint>
#include <vector>

#include "world/collision.h"
#include "world/lights.h"
#include "world/room_builder.h"

namespace museum::world {

// A single vertex: position, normal, UV, and the light anchor copied per vertex
// so the later bake needs no extra lookups.
struct Vertex {
  float px = 0.0f;
  float py = 0.0f;
  float pz = 0.0f;
  float nx = 0.0f;
  float ny = 0.0f;
  float nz = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
  // Material selector, so one draw call carries every surface. The shader
  // switches on it: 0 = wall/ceiling (lit, wall colour), 1 = floor (lit, floor
  // texture), 2 = painting canvas (unshaded, from the paintings atlas, with the
  // UV already in atlas space), 3 = painting frame (lit, dark grey).
  float material = 0.0f;
};

// Legacy alias: the floor range used to be flagged with a boolean, and the
// renderer's floor_index_* range still marks it.
inline constexpr float kMaterialWall = 0.0f;
inline constexpr float kMaterialFloor = 1.0f;
inline constexpr float kMaterialPainting = 2.0f;
inline constexpr float kMaterialFrame = 3.0f;

struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;

  // The range of indices that belongs to floors, kept so the renderer can tell
  // the floor span apart in profiling and so the shader contract stays obvious.
  std::uint32_t floor_index_begin = 0;
  std::uint32_t floor_index_count = 0;

  // The range of indices that belongs to painting canvases. Drawn after the
  // floors, unshaded, from the atlas.
  std::uint32_t painting_index_begin = 0;
  std::uint32_t painting_index_count = 0;
};

struct MuseumStats {
  int rooms = 0;
  int segments = 0;
  int colliders = 0;
  int painting_spots = 0;
  float min_y = 0.0f;
  float max_y = 0.0f;
};

class Museum {
 public:
  // Builds geometry and colliders from the layout. Call once at startup.
  void Build();

  // Emits the museum into one merged, indexed mesh: walls and ceilings first,
  // then the floor triangles, then the painting canvases. Floors and paintings
  // each occupy a contiguous index range the renderer binds a material to.
  //
  // `painting_uvs` supplies each painting's rectangle in the atlas, in painting
  // order. When it is empty (or shorter than the number of spots) the paintings
  // are still built, so the frames are visible, but the canvases are omitted
  // rather than sampled from nowhere.
  struct PaintingUv {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float aspect = 1.0f;
  };
  void EmitMesh(Mesh& out, const std::vector<PaintingUv>& painting_uvs) const;

  const CollisionWorld& collision() const { return collision_; }
  const MuseumStats& stats() const { return stats_; }
  const std::vector<SpotLight>& lights() const { return lights_; }
  const std::vector<PaintingSpot>& painting_spots() const {
    return painting_spots_;
  }

 private:
  void AddSegment(const SegmentBox& box);
  void AddCornerPatches();

  std::vector<SegmentBox> segments_;
  std::vector<PaintingSpot> painting_spots_;
  std::vector<SpotLight> lights_;
  CollisionWorld collision_;
  MuseumStats stats_;
};

}  // namespace museum::world
