// Minimal swept AABB collision, replacing Godot's CharacterBody3D.move_and_slide.
// Port of the JS museum's Colliders.ts.
//
// The whole museum is axis-aligned boxes, so per-axis resolution is exact. The
// two subtleties worth preserving are the axis ordering on Y (solid, then
// dynamic, then floor) and the minimum-translation test, which is what stops a
// full-height wall from shoving the player down through the floor.
#pragma once

#include <vector>

#include "core/math.h"

namespace museum::world {

struct AABBBox {
  float min_x = 0.0f;
  float min_y = 0.0f;
  float min_z = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  float max_z = 0.0f;
};

// Half-extents of the player's collision box.
struct HalfExtents {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

class CollisionWorld {
 public:
  // All colliders, then solid-only and floor-only views of the same boxes.
  std::vector<AABBBox> boxes;
  std::vector<AABBBox> solid_boxes;
  std::vector<AABBBox> floor_boxes;

  // Moving colliders (elevator car and doors); refreshed each frame in Batch 7.
  std::vector<AABBBox> dynamic_boxes;

  void AddBox(float cx, float cy, float cz, float sx, float sy, float sz,
              bool is_floor) {
    AABBBox box;
    box.min_x = cx - sx * 0.5f;
    box.min_y = cy - sy * 0.5f;
    box.min_z = cz - sz * 0.5f;
    box.max_x = cx + sx * 0.5f;
    box.max_y = cy + sy * 0.5f;
    box.max_z = cz + sz * 0.5f;

    boxes.push_back(box);
    (is_floor ? floor_boxes : solid_boxes).push_back(box);
  }

  // Moves `position` (the player box centre) by `delta`, resolving per axis:
  // X, then Z, then Y. Returns true when the move landed on ground.
  // Const because the collider set is immutable once built; only the caller's
  // position is written.
  bool Move(math::Vec3& position, const HalfExtents& half,
            const math::Vec3& delta) const {
    position.x += delta.x;
    if (delta.x != 0.0f) {
      ResolveAxis(position, half, 0, delta.x);
    }

    position.z += delta.z;
    if (delta.z != 0.0f) {
      ResolveAxis(position, half, 2, delta.z);
    }

    position.y += delta.y;
    bool on_ground = false;
    if (delta.y != 0.0f) {
      const float y_before = position.y;
      const bool hit = ResolveAxis(position, half, 1, delta.y);
      // Grounded only when the resolver pushed us UP onto a surface; being
      // ejected downward from a ceiling is not ground.
      if (hit && delta.y < 0.0f && position.y >= y_before) {
        on_ground = true;
      }
    }
    return on_ground;
  }

 private:
  static constexpr float kEps = 1e-4f;

  bool ResolveAxis(math::Vec3& position, const HalfExtents& half, int axis,
                   float amount) const {
    bool hit = false;

    // On Y, walkable floors resolve LAST so a wall's downward push can never
    // override the floor supporting the player.
    if (axis == 1) {
      for (const AABBBox& box : solid_boxes) {
        if (ResolveOne(position, half, axis, box)) hit = true;
      }
      for (const AABBBox& box : dynamic_boxes) {
        if (ResolveOne(position, half, axis, box)) hit = true;
      }
      for (const AABBBox& box : floor_boxes) {
        if (ResolveOne(position, half, axis, box)) hit = true;
      }
    } else {
      for (const AABBBox& box : boxes) {
        if (ResolveOne(position, half, axis, box)) hit = true;
      }
      for (const AABBBox& box : dynamic_boxes) {
        if (ResolveOne(position, half, axis, box)) hit = true;
      }
    }
    (void)amount;
    return hit;
  }

  // Minimum-translation resolve: push the player out through the NEAREST face,
  // and only along this pass's axis when that axis is the box's
  // minimal-penetration axis.
  static bool ResolveOne(math::Vec3& position, const HalfExtents& half, int axis,
                         const AABBBox& box) {
    const float pmin_x = position.x - half.x;
    const float pmax_x = position.x + half.x;
    const float pmin_y = position.y - half.y;
    const float pmax_y = position.y + half.y;
    const float pmin_z = position.z - half.z;
    const float pmax_z = position.z + half.z;

    if (pmin_x >= box.max_x || pmax_x <= box.min_x) return false;
    if (pmin_y >= box.max_y || pmax_y <= box.min_y) return false;
    if (pmin_z >= box.max_z || pmax_z <= box.min_z) return false;

    const float pen_x0 = pmax_x - box.min_x;
    const float pen_x1 = box.max_x - pmin_x;
    const float pen_y0 = box.max_y - pmin_y;
    const float pen_y1 = pmax_y - box.min_y;
    const float pen_z0 = pmax_z - box.min_z;
    const float pen_z1 = box.max_z - pmin_z;

    const float pen_x = math::Min(pen_x0, pen_x1);
    const float pen_y = math::Min(pen_y0, pen_y1);
    const float pen_z = math::Min(pen_z0, pen_z1);

    if (axis == 0 && (pen_y < pen_x || pen_z < pen_x)) return false;
    if (axis == 1 && (pen_x < pen_y || pen_z < pen_y)) return false;
    if (axis == 2 && (pen_x < pen_z || pen_y < pen_z)) return false;

    if (axis == 0) {
      position.x = pen_x0 < pen_x1 ? box.min_x - half.x - kEps
                                   : box.max_x + half.x + kEps;
    } else if (axis == 1) {
      position.y = pen_y0 < pen_y1 ? box.max_y + half.y + kEps
                                   : box.min_y - half.y - kEps;
    } else {
      position.z = pen_z0 < pen_z1 ? box.min_z - half.z - kEps
                                   : box.max_z + half.z + kEps;
    }
    return true;
  }
};

}  // namespace museum::world
