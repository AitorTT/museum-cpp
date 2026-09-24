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
};

struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;

  // Part of the mesh that belongs to floors, so it can be given the floor
  // material (and its texture) when the renderer gains materials.
  std::uint32_t floor_index_begin = 0;
  std::uint32_t floor_index_count = 0;
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
  // then the floor triangles. Floors come last so they occupy a contiguous
  // index range the renderer can bind a floor material to.
  void EmitMesh(Mesh& out) const;

  const CollisionWorld& collision() const { return collision_; }
  const MuseumStats& stats() const { return stats_; }

 private:
  void AddSegment(const SegmentBox& box);
  void AddCornerPatches();

  std::vector<SegmentBox> segments_;
  std::vector<PaintingSpot> painting_spots_;
  CollisionWorld collision_;
  MuseumStats stats_;
};

}  // namespace museum::world
