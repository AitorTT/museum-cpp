#include "render/painting_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "stb_image.h"

namespace museum::render {
namespace {

// The 27 canvases, in the JS museum's assignment order. Naming them explicitly
// rather than scanning the directory keeps this identical on web and native
// (Emscripten's virtual filesystem has no cheap directory listing) and pins the
// order to the one paintingList.ts uses, so a painting lands in the same room on
// both builds.
constexpr const char* kPaintingFiles[] = {
    "painting_01.jpg", "painting_02.jpg", "painting_03.jpg", "painting_04.jpg",
    "painting_05.jpg", "painting_06.jpg", "painting_07.jpg", "painting_08.jpg",
    "painting_09.jpg", "painting_10.jpg", "painting_11.jpg", "painting_12.jpg",
    "painting_13.jpg", "painting_14.jpg", "painting_15.jpg", "painting_16.jpg",
    "painting_17.jpg", "painting_18.jpg", "painting_19.jpg", "painting_20.jpg",
    "painting_21.jpg", "painting_22.jpg", "painting_23.jpg", "painting_24.jpg",
    "painting_25.jpg", "painting_26.jpg", "painting_27.jpg",
};

constexpr int kPaintingCount =
    static_cast<int>(sizeof(kPaintingFiles) / sizeof(kPaintingFiles[0]));

// Nearest-neighbour box resize of an RGBA8 image. Average-of-area is not needed
// at this reduction, and nearest keeps the code small; the source images are
// photographs, so the only visible cost is a slightly harder edge.
void ResizeInto(const unsigned char* src, int sw, int sh, unsigned char* dst,
                int dw, int dh) {
  for (int y = 0; y < dh; ++y) {
    const int sy = std::min(sh - 1, (y * sh) / dh);
    for (int x = 0; x < dw; ++x) {
      const int sx = std::min(sw - 1, (x * sw) / dw);
      const unsigned char* s = src + (static_cast<std::size_t>(sy) * sw + sx) * 4;
      unsigned char* d = dst + (static_cast<std::size_t>(y) * dw + x) * 4;
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
    }
  }
}

// One shelf in the packing: a horizontal strip of uniform height.
struct Shelf {
  int y = 0;
  int height = 0;
  int cursor_x = 0;
};

}  // namespace

PaintingAtlas BuildPaintingAtlas(const std::string& dir, const char* /*extension*/) {
  // Decode and scale first, so the packer knows every cell before it lays out.
  struct Scaled {
    std::vector<unsigned char> pixels;
    int w = 0;
    int h = 0;
    float aspect = 1.0f;
  };
  std::vector<Scaled> scaled;
  scaled.reserve(kPaintingCount);

  for (int i = 0; i < kPaintingCount; ++i) {
    const std::string path = dir + "/" + kPaintingFiles[i];
    int sw = 0;
    int sh = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &sw, &sh, &channels, 4);
    if (data == nullptr) {
      std::printf("PaintingAtlas: failed to load %s\n", path.c_str());
      // Leave a hole rather than aborting the whole museum: one missing
      // painting is survivable, a blank wall everywhere is not.
      scaled.push_back(Scaled{});
      continue;
    }

    const int long_side = std::max(sw, sh);
    const float ratio = static_cast<float>(kPaintingLongSide) /
                        static_cast<float>(long_side > 0 ? long_side : 1);
    int dw = std::max(1, static_cast<int>(std::lround(sw * ratio)));
    int dh = std::max(1, static_cast<int>(std::lround(sh * ratio)));

    Scaled s;
    s.w = dw;
    s.h = dh;
    s.aspect = sh > 0 ? static_cast<float>(sw) / static_cast<float>(sh) : 1.0f;
    s.pixels.resize(static_cast<std::size_t>(dw) * dh * 4);
    ResizeInto(data, sw, sh, s.pixels.data(), dw, dh);
    stbi_image_free(data);
    scaled.push_back(std::move(s));
  }

  // Shelf packing: sort by height descending so shelves stay tight, but keep a
  // parallel index so entries can be written back in load order.
  std::vector<int> order(kPaintingCount);
  for (int i = 0; i < kPaintingCount; ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&scaled](int a, int b) {
    return scaled[a].h > scaled[b].h;
  });

  const int atlas_width = 4096;
  std::vector<Shelf> shelves;
  std::vector<std::pair<int, int>> placed(kPaintingCount, {0, 0});

  for (int idx : order) {
    const Scaled& s = scaled[idx];
    if (s.w == 0) continue;  // the hole from a failed decode

    Shelf* target = nullptr;
    for (Shelf& shelf : shelves) {
      if (s.h <= shelf.height && shelf.cursor_x + s.w <= atlas_width) {
        target = &shelf;
        break;
      }
    }
    if (target == nullptr) {
      Shelf fresh;
      fresh.y = shelves.empty() ? 0 : shelves.back().y + shelves.back().height;
      fresh.height = s.h;
      shelves.push_back(fresh);
      target = &shelves.back();
    }
    placed[idx] = {target->cursor_x, target->y};
    target->cursor_x += s.w;
  }

  int atlas_height = 0;
  for (const Shelf& shelf : shelves) {
    atlas_height = std::max(atlas_height, shelf.y + shelf.height);
  }
  if (atlas_height <= 0) {
    return PaintingAtlas{};
  }

  PaintingAtlas atlas;
  atlas.image.width = atlas_width;
  atlas.image.height = atlas_height;
  atlas.image.pixels.assign(
      static_cast<std::size_t>(atlas_width) * atlas_height * 4, 0);
  atlas.entries.resize(kPaintingCount);

  for (int i = 0; i < kPaintingCount; ++i) {
    const Scaled& s = scaled[i];
    PaintingAtlasEntry e;
    if (s.w > 0) {
      const int px = placed[i].first;
      const int py = placed[i].second;
      for (int y = 0; y < s.h; ++y) {
        const std::size_t src = static_cast<std::size_t>(y) * s.w * 4;
        const std::size_t dst =
            (static_cast<std::size_t>(py + y) * atlas_width + px) * 4;
        std::memcpy(atlas.image.pixels.data() + dst, s.pixels.data() + src,
                    static_cast<std::size_t>(s.w) * 4);
      }
      e.u0 = static_cast<float>(px) / static_cast<float>(atlas_width);
      e.v0 = static_cast<float>(py) / static_cast<float>(atlas_height);
      e.u1 = static_cast<float>(px + s.w) / static_cast<float>(atlas_width);
      e.v1 = static_cast<float>(py + s.h) / static_cast<float>(atlas_height);
      e.aspect = s.aspect;
    }
    atlas.entries[i] = e;
  }

  std::printf("PaintingAtlas: %d paintings packed into %dx%d (%zu KB)\n",
              kPaintingCount, atlas_width, atlas_height,
              atlas.image.pixels.size() / 1024);
  return atlas;
}

}  // namespace museum::render
