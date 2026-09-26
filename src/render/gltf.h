// A minimal glTF 2.0 reader, just enough for the museum's one sculpture.
//
// The sculpture (assets/models/Untitled.glb) is a photogrammetry scan: a single
// mesh with POSITION/NORMAL/TEXCOORD_0 and an index buffer, plus two embedded
// JPEGs (base colour and a tangent-space normal map). That is the whole feature
// surface this reader needs, so it is written by hand rather than pulling in
// cgltf or tinygltf: the project avoids dependencies it does not exercise, and
// the format subset is small and fully specified.
//
// What it does NOT do, on purpose: sparse accessors, animation, skinning,
// multiple scenes, Draco compression, .gltf-with-external-buffers. The museum
// owns this one file and can guarantee it stays inside the subset. If the
// sculpture is ever replaced, these assumptions are what to check first.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"
#include "render/texture.h"

namespace museum::render {

// One vertex of the sculpture, already put through the glTF node transform so
// its positions are in the museum's world space (metres).
struct GltfVertex {
  float px = 0.0f;
  float py = 0.0f;
  float pz = 0.0f;
  float nx = 0.0f;
  float ny = 0.0f;
  float nz = 0.0f;
  float u = 0.0f;
  float v = 0.0f;
};

struct GltfModel {
  std::vector<GltfVertex> vertices;
  std::vector<std::uint32_t> indices;

  // The two embedded images, decoded. `base_color` is the one the material's
  // baseColorTexture points at, `normal` the normalTexture. Either may be empty
  // if the file does not carry it; the caller decides how to fall back.
  Image base_color;
  Image normal;

  bool empty() const { return vertices.empty() || indices.empty(); }
};

// Reads a .glb from disk (or the Emscripten virtual filesystem). Reports why and
// returns an empty model on any failure, so one bad asset is never fatal.
GltfModel LoadGlb(const std::string& path);

}  // namespace museum::render
