#include "world/shadows.h"

#include <cmath>

#include "world/config.h"

namespace museum::world {

// How far the fixture hangs above the floor it lights: the anchor sits
// kHalfHeight - kWallThickness - 0.05 below the floor's centre, and the floor
// top is kHalfHeight - kWallThickness above it (see config::FloorTopY). Both
// terms are the same for every floor, so this is a constant.
constexpr float kFixtureHeight =
    2.0f * (config::kRoomHeight * 0.5f) -
    2.0f * config::kWallThickness - 0.05f;

ShadowView BuildShadowView(const SpotLight& light, float extent, float depth,
                           std::uint32_t tile_index) {
  ShadowView view;

  // Look straight down from the fixture. The up vector must not be parallel to
  // the direction, or the basis collapses; -Z is always safe for a downward
  // light.
  const Vec3 eye = light.position;
  const Vec3 forward = math::Normalize(light.direction);
  const Vec3 up = {0.0f, 0.0f, -1.0f};

  // Right-handed look-at, matching the camera's convention.
  const Vec3 z_axis = -forward;  // camera looks down its own -Z
  const Vec3 x_axis = math::Normalize(math::Cross(up, z_axis));
  const Vec3 y_axis = math::Cross(z_axis, x_axis);

  math::Mat4 view_matrix = math::Mat4::Identity();
  view_matrix.m[0] = x_axis.x;
  view_matrix.m[4] = x_axis.y;
  view_matrix.m[8] = x_axis.z;
  view_matrix.m[1] = y_axis.x;
  view_matrix.m[5] = y_axis.y;
  view_matrix.m[9] = y_axis.z;
  view_matrix.m[2] = z_axis.x;
  view_matrix.m[6] = z_axis.y;
  view_matrix.m[10] = z_axis.z;
  view_matrix.m[12] = -math::Dot(x_axis, eye);
  view_matrix.m[13] = -math::Dot(y_axis, eye);
  view_matrix.m[14] = -math::Dot(z_axis, eye);

  // Perspective, not orthographic. An orthographic top-down map treats the
  // light as a parallel beam: a doorway's lintel sits directly above its
  // threshold in light space, so the vertical ray reads "occluded" and paints a
  // dark band across the doorway floor. The fixture is 5 m off to the side, and
  // the real ray to that floor is diagonal and clears the lintel. A perspective
  // projection from the fixture's actual position gets that right.
  //
  // `extent` is the half-width the tile must cover on the floor; the field of
  // view that reaches it from the fixture is 2*atan(extent / height).
  const float near_plane = 0.1f;
  const float far_plane = depth;
  const float half_fov = std::atan2(extent, kFixtureHeight);
  const math::Mat4 projection =
      math::Mat4::Perspective(2.0f * half_fov, 1.0f, near_plane, far_plane);
  view.view_projection = projection * view_matrix;

  const std::uint32_t column = tile_index % kShadowAtlasColumns;
  const std::uint32_t row = (tile_index / kShadowAtlasColumns) % kShadowAtlasRows;
  const float tile =
      1.0f / static_cast<float>(kShadowAtlasColumns);

  // Inset by half a texel so PCF taps near a tile edge do not bleed into the
  // neighbouring light's depth.
  const float texel = tile / static_cast<float>(kShadowTileSize);
  const float half_texel = texel * 0.5f;
  view.atlas_rect[0] = static_cast<float>(column) * tile + half_texel;
  view.atlas_rect[1] = static_cast<float>(row) * tile + half_texel;
  view.atlas_rect[2] = tile - texel;
  view.atlas_rect[3] = tile - texel;

  return view;
}

}  // namespace museum::world
