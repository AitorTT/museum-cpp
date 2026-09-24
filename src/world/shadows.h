// Shadow mapping for the ceiling spots.
//
// Each light renders the museum from its own point of view into a tile of one
// large depth texture. An atlas rather than one texture per light because there
// are 43 rooms: 43 render passes is fine (they are small), but 43 textures and
// bind groups is not.
//
// Viewing frustum: a light only needs to cover its own room and a little way
// past the doorway, since its cone fades to nothing at 15u and its range is
// bounded. That keeps each tile's depth range tight, which is what makes an
// 8-bit-free Depth32Float atlas sharpen up at a modest resolution.
#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "world/lights.h"

namespace museum::world {

// Shared sizing for the shadow atlas, sized for the museum's 43 lights.
//
// Tile size in texels. An 8x8 grid at 512 would be 4096x4096, or 64 MB of
// Depth32Float: affordable but wasteful. 256 gives 2048x2048 (16 MB) and, over
// a 9-unit ortho extent, roughly 3.5 cm per texel, which is plenty for soft
// room-scale shadows.
inline constexpr std::uint32_t kShadowTileSize = 256;
inline constexpr std::uint32_t kShadowAtlasColumns = 8;
inline constexpr std::uint32_t kShadowAtlasRows = 8;
inline constexpr std::uint32_t kShadowAtlasSize =
    kShadowTileSize * kShadowAtlasColumns;

// One light's slot in the atlas plus the matrices that put its tile on screen.
struct ShadowView {
  math::Mat4 view_projection;
  // Atlas UV rectangle: [x, y] origin, [z, w] size, in normalised coordinates.
  float atlas_rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Builds a light-space matrix for a downward spot.
//
// The projection is orthographic rather than perspective: at a 15u range with a
// 60-degree cone the footprint is about 17u across, and an ortho box of that
// size is both simpler to fit and free of the perspective-depth precision loss
// that makes shadow acne hard to tune in a small tile.
ShadowView BuildShadowView(const SpotLight& light, float extent, float depth,
                           std::uint32_t tile_index);

}  // namespace museum::world
