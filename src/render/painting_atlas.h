// The paintings atlas: 27 canvases packed into one texture.
//
// One texture for every painting rather than one texture each. The museum is
// static and all 27 are visible from somewhere, so a single binding serves the
// whole scene pass: no per-painting bind group, and no 27-way texture switch as
// the camera turns. The cost is that a painting's canvas is drawn from a
// sub-rectangle, which the vertex carries as a UV already scaled into its cell.
//
// The images arrive at around 1200px on the long side and in mixed orientations
// (some portrait, some landscape), so a fixed grid would waste most of the
// atlas. Instead each is scaled to a common long side and packed onto shelves,
// which keeps the atlas near-square without any per-image pixel reflow.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include "render/texture.h"

namespace museum::render {

// Long side every painting is scaled to before packing. 512 is a compromise:
// the canvases are read from across a room, not pressed against, and it keeps
// the whole atlas inside a single 4096-wide texture.
inline constexpr int kPaintingLongSide = 512;

// Where one painting sits in the atlas, in UV space. The painting's aspect is
// preserved so the frame can be built without re-deriving it from the image.
struct PaintingAtlasEntry {
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  float aspect = 1.0f;  // width / height of the source image
};

struct PaintingAtlas {
  Image image;                              // the packed RGBA8 pixels
  std::vector<PaintingAtlasEntry> entries;  // one per painting, in load order

  bool empty() const { return image.empty() || entries.empty(); }
};

// Decodes every file in `dir` in sorted order and packs it. Returns an atlas
// whose `entries` line up with the sorted file order, so caller code can index
// them by painting number. Reports the first failure and returns an empty atlas
// if nothing could be read.
PaintingAtlas BuildPaintingAtlas(const std::string& dir, const char* extension);

}  // namespace museum::render
