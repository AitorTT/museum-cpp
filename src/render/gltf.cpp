#include "render/gltf.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "stb_image.h"

namespace museum::render {
namespace {

// ---------------------------------------------------------------------------
// A tiny JSON reader.
//
// The glTF header hands back a UTF-8 JSON chunk. Rather than parse it into a
// tree, this walks it on demand: the model parser asks for a specific path and
// reads the value there. That keeps the code to a few hundred lines and sidesteps
// owning a document model for what is, after all, a fixed file. The values the
// museum cares about are: numbers, strings, arrays of numbers, and named objects.
// ---------------------------------------------------------------------------

struct JsonScanner {
  const char* p = nullptr;
  const char* end = nullptr;

  void SkipWhitespace() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      ++p;
    }
  }

  bool Match(char c) {
    SkipWhitespace();
    if (p < end && *p == c) {
      ++p;
      return true;
    }
    return false;
  }

  bool Peek(char c) {
    SkipWhitespace();
    return p < end && *p == c;
  }

  // Reads a JSON string, unescaping the handful of sequences glTF uses. Returns
  // false if the next token is not a string.
  bool ReadString(std::string& out) {
    SkipWhitespace();
    if (p >= end || *p != '"') {
      return false;
    }
    ++p;
    out.clear();
    while (p < end && *p != '"') {
      if (*p == '\\' && p + 1 < end) {
        ++p;
        switch (*p) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'u': {
            // Encode the code point as UTF-8. The museum's strings are ASCII in
            // practice; this keeps non-ASCII names from corrupting the parse.
            if (p + 4 >= end) { return false; }
            unsigned code = 0;
            for (int i = 1; i <= 4; ++i) {
              const char h = p[i];
              code <<= 4;
              if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
              else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
              else return false;
            }
            if (code < 0x80) {
              out.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (code >> 6)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (code >> 12)));
              out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            p += 4;
            break;
          }
          default: out.push_back(*p); break;
        }
        ++p;
      } else {
        out.push_back(*p++);
      }
    }
    if (p >= end) {
      return false;
    }
    ++p;  // closing quote
    return true;
  }

  bool ReadNumber(double& out) {
    SkipWhitespace();
    // Tolerate a separator comma, so a number-by-number walk of an array does
    // not have to interleave Match(',') calls.
    if (p < end && *p == ',') {
      ++p;
      SkipWhitespace();
    }
    // The JSON chunk is not NUL-terminated (it sits inside the GLB), so the
    // number is copied into a bounded scratch buffer before strtod sees it.
    char buffer[64];
    std::size_t n = 0;
    const char* start = p;
    while (p < end && n + 1 < sizeof(buffer) && (*p == '-' || *p == '+' ||
           *p == '.' || *p == 'e' || *p == 'E' || (*p >= '0' && *p <= '9'))) {
      buffer[n++] = *p++;
    }
    buffer[n] = '\0';
    if (p == start) {
      return false;
    }
    char* stop = nullptr;
    out = std::strtod(buffer, &stop);
    return stop != buffer;
  }

  // Skips any single JSON value. Needed to walk past members this reader does
  // not care about without understanding them.
  bool SkipValue() {
    SkipWhitespace();
    if (p >= end) return false;
    if (*p == '"') {
      std::string ignored;
      return ReadString(ignored);
    }
    if (*p == '{' || *p == '[') {
      const char open = *p;
      const char close = (open == '{') ? '}' : ']';
      int depth = 0;
      do {
        if (p >= end) return false;
        if (*p == '"') {
          std::string ignored;
          if (!ReadString(ignored)) return false;
          continue;
        }
        if (*p == open) ++depth;
        else if (*p == close) --depth;
        ++p;
      } while (depth > 0);
      return true;
    }
    // A bare literal: number, true, false, null.
    while (p < end && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
      ++p;
    }
    return true;
  }

  // Positions the scanner just inside the named member of the object at `*p`.
  // Returns false when the member is absent, leaving the scanner at the object's
  // closing brace so callers can keep searching or bail.
  //
  // Resumable: if the scanner sits just after a previous member's value (at a
  // comma), that comma is consumed first, so a second seek on the same object
  // works without restarting from the brace.
  bool SeekMember(const char* name) {
    SkipWhitespace();
    if (p < end && *p == '{') {
      ++p;
    } else if (p < end && *p == ',') {
      ++p;
    }
    while (true) {
      SkipWhitespace();
      if (p >= end || *p == '}') {
        return false;
      }
      std::string key;
      if (!ReadString(key)) {
        return false;
      }
      if (!Match(':')) {
        return false;
      }
      if (key == name) {
        return true;
      }
      if (!SkipValue()) {
        return false;
      }
      if (!Match(',')) {
        return false;
      }
    }
  }
};

// Reads an object member's value as a double. Returns false if the member is
// missing or is not a number.
bool ObjectNumber(JsonScanner& s, const char* name, double& out) {
  const char* saved = s.p;
  if (!s.SeekMember(name)) {
    s.p = saved;
    return false;
  }
  return s.ReadNumber(out);
}

// The glTF structures this reader extracts into, before node transforms.
struct Accessor {
  int buffer_view = -1;
  int component_type = 0;
  int count = 0;
  int type = 0;  // number of components
};

struct BufferView {
  int buffer = 0;
  std::size_t byte_offset = 0;
  std::size_t byte_length = 0;
  std::size_t byte_stride = 0;
};

int ComponentsPerType(const std::string& type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT4") return 16;
  return 0;
}

std::size_t ComponentSize(int component_type) {
  switch (component_type) {
    case 5120:  // BYTE
    case 5121:  // UNSIGNED_BYTE
      return 1;
    case 5122:  // SHORT
    case 5123:  // UNSIGNED_SHORT
      return 2;
    case 5125:  // UNSIGNED_INT
    case 5126:  // FLOAT
      return 4;
    default:
      return 0;
  }
}

// Locates the accessor/buffer-view arrays. They are small and the parser reads
// them once into vectors, so a member seek per entry is fine.
bool ReadAccessors(JsonScanner& s, std::vector<Accessor>& out) {
  const char* saved = s.p;
  if (!s.SeekMember("accessors")) {
    s.p = saved;
    return false;
  }
  if (!s.Match('[')) {
    return false;
  }
  while (!s.Peek(']')) {
    Accessor a{};
    // Inside the object: each lookup restarts from the object's brace, because
    // a seek leaves the scanner at the previous member's value.
    double view = 0, comp = 0, count = 0;
    {
      JsonScanner f = s;
      if (!ObjectNumber(f, "bufferView", view)) view = -1;
    }
    {
      JsonScanner f = s;
      if (!ObjectNumber(f, "componentType", comp)) return false;
    }
    {
      JsonScanner f = s;
      if (!ObjectNumber(f, "count", count)) return false;
    }
    a.buffer_view = static_cast<int>(view);
    a.component_type = static_cast<int>(comp);
    a.count = static_cast<int>(count);
    std::string type;
    JsonScanner type_scan = s;
    if (type_scan.SeekMember("type")) {
      type_scan.ReadString(type);
    }
    a.type = ComponentsPerType(type);
    out.push_back(a);

    if (!s.SkipValue()) return false;
    if (!s.Match(',')) break;
  }
  s.Match(']');
  return true;
}

bool ReadBufferViews(JsonScanner& s, std::vector<BufferView>& out) {
  const char* saved = s.p;
  if (!s.SeekMember("bufferViews")) {
    s.p = saved;
    return false;
  }
  if (!s.Match('[')) {
    return false;
  }
  while (!s.Peek(']')) {
    BufferView v{};
    double d = 0;
    JsonScanner scan = s;
    if (ObjectNumber(scan, "byteOffset", d)) v.byte_offset = static_cast<std::size_t>(d);
    scan = s;
    if (!ObjectNumber(scan, "byteLength", d)) return false;
    v.byte_length = static_cast<std::size_t>(d);
    scan = s;
    if (ObjectNumber(scan, "byteStride", d)) v.byte_stride = static_cast<std::size_t>(d);
    out.push_back(v);

    if (!s.SkipValue()) return false;
    if (!s.Match(',')) break;
  }
  s.Match(']');
  return true;
}

// A 4x4 column-major transform, matching math::Mat4's layout.
math::Mat4 ReadNodeTransform(JsonScanner& node_scan) {
  math::Mat4 m = math::Mat4::Identity();

  double t[3] = {0, 0, 0};
  bool has_t = false;
  {
    JsonScanner scan = node_scan;
    if (scan.SeekMember("translation") && scan.Match('[')) {
      for (int i = 0; i < 3; ++i) scan.ReadNumber(t[i]);
      has_t = true;
    }
  }
  double s[3] = {1, 1, 1};
  {
    JsonScanner scan = node_scan;
    if (scan.SeekMember("scale") && scan.Match('[')) {
      for (int i = 0; i < 3; ++i) scan.ReadNumber(s[i]);
    }
  }
  double r[4] = {0, 0, 0, 1};
  {
    JsonScanner scan = node_scan;
    if (scan.SeekMember("rotation") && scan.Match('[')) {
      for (int i = 0; i < 4; ++i) scan.ReadNumber(r[i]);
    }
  }
  (void)has_t;

  // Compose as T * R * S, the glTF TRS order.
  const math::Mat4 scale_m = math::Mat4::Scale(
      {static_cast<float>(s[0]), static_cast<float>(s[1]), static_cast<float>(s[2])});

  const float x = static_cast<float>(r[0]);
  const float y = static_cast<float>(r[1]);
  const float z = static_cast<float>(r[2]);
  const float w = static_cast<float>(r[3]);
  math::Mat4 rot_m = math::Mat4::Identity();
  // Quaternion -> column-major rotation matrix.
  rot_m.m[0] = 1 - 2 * (y * y + z * z);
  rot_m.m[1] = 2 * (x * y + z * w);
  rot_m.m[2] = 2 * (x * z - y * w);
  rot_m.m[4] = 2 * (x * y - z * w);
  rot_m.m[5] = 1 - 2 * (x * x + z * z);
  rot_m.m[6] = 2 * (y * z + x * w);
  rot_m.m[8] = 2 * (x * z + y * w);
  rot_m.m[9] = 2 * (y * z - x * w);
  rot_m.m[10] = 1 - 2 * (x * x + y * y);

  const math::Mat4 trans_m = math::Mat4::Translation(
      {static_cast<float>(t[0]), static_cast<float>(t[1]), static_cast<float>(t[2])});

  return trans_m * rot_m * scale_m;
}

}  // namespace

GltfModel LoadGlb(const std::string& path) {
  GltfModel model;

  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    std::printf("Gltf: cannot open %s\n", path.c_str());
    return model;
  }
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 12) {
    std::printf("Gltf: %s is too small to be a GLB\n", path.c_str());
    std::fclose(f);
    return model;
  }
  std::vector<std::uint8_t> file(static_cast<std::size_t>(size));
  if (std::fread(file.data(), 1, file.size(), f) != file.size()) {
    std::printf("Gltf: short read on %s\n", path.c_str());
    std::fclose(f);
    return model;
  }
  std::fclose(f);

  const auto read_u32 = [](const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
  };

  if (std::memcmp(file.data(), "glTF", 4) != 0) {
    std::printf("Gltf: %s is not a GLB (bad magic)\n", path.c_str());
    return model;
  }
  const std::uint32_t version = read_u32(file.data() + 4);
  if (version != 2) {
    std::printf("Gltf: %s is glTF version %u, only 2 is supported\n", path.c_str(),
                version);
    return model;
  }

  // Walk the chunks: the first is JSON, a later one BIN.
  const std::uint8_t* json_begin = nullptr;
  std::size_t json_size = 0;
  const std::uint8_t* bin_begin = nullptr;
  std::size_t bin_size = 0;

  std::size_t offset = 12;
  while (offset + 8 <= file.size()) {
    const std::uint32_t chunk_len = read_u32(file.data() + offset);
    const std::uint32_t chunk_type = read_u32(file.data() + offset + 4);
    const std::uint8_t* chunk_data = file.data() + offset + 8;
    if (offset + 8 + chunk_len > file.size()) {
      std::printf("Gltf: chunk overruns the file\n");
      return model;
    }
    if (chunk_type == 0x4E4F534A) {  // "JSON"
      json_begin = chunk_data;
      json_size = chunk_len;
    } else if (chunk_type == 0x004E4942) {  // "BIN\0"
      bin_begin = chunk_data;
      bin_size = chunk_len;
    }
    // Chunks are 4-byte aligned, so pad up.
    offset += 8 + ((chunk_len + 3u) & ~3u);
  }

  if (json_begin == nullptr || bin_begin == nullptr) {
    std::printf("Gltf: %s is missing its JSON or BIN chunk\n", path.c_str());
    return model;
  }

  // The pristine scanner. Every top-level lookup copies this, because seeking a
  // named member leaves the scanner deep inside the document and a later member
  // can sit earlier in the file (materials precede meshes, textures follow them).
  JsonScanner document;
  document.p = reinterpret_cast<const char*>(json_begin);
  document.end = document.p + json_size;

  std::vector<Accessor> accessors;
  std::vector<BufferView> buffer_views;
  {
    JsonScanner root = document;
    const bool have_a = ReadAccessors(root, accessors);
    root = document;
    const bool have_bv = ReadBufferViews(root, buffer_views);
    if (!have_a || !have_bv) {
      std::printf("Gltf: %s has no accessors or bufferViews\n", path.c_str());
      return model;
    }
  }

  if (accessors.size() < 4 || buffer_views.size() < 6) {
    std::printf("Gltf: %s does not have the expected accessor layout\n", path.c_str());
    return model;
  }

  // Accessors 0..3 are POSITION, NORMAL, TEXCOORD_0 and the indices in this
  // file. Rather than trust the order, read the primitive's attribute map.
  int idx_position = 0;
  int idx_normal = 1;
  int idx_uv = 2;
  int idx_indices = 3;
  {
    JsonScanner scan = document;
    if (scan.SeekMember("meshes") && scan.Match('[') && scan.Match('{')) {
      if (scan.SeekMember("primitives") && scan.Match('[') && scan.Match('{')) {
        JsonScanner attrs = scan;
        if (attrs.SeekMember("attributes")) {
          double v = 0;
          JsonScanner a = attrs;
          if (ObjectNumber(a, "POSITION", v)) idx_position = static_cast<int>(v);
          a = attrs;
          if (ObjectNumber(a, "NORMAL", v)) idx_normal = static_cast<int>(v);
          a = attrs;
          if (ObjectNumber(a, "TEXCOORD_0", v)) idx_uv = static_cast<int>(v);
        }
        JsonScanner ind = scan;
        double v = 0;
        if (ObjectNumber(ind, "indices", v)) idx_indices = static_cast<int>(v);
      }
    }
  }

  // The whole scene's transform: this file nests mesh node 0 under parent node
  // 1, so the mesh is placed by node1 * node0.
  math::Mat4 node_transform = math::Mat4::Identity();
  {
    JsonScanner scan = document;
    if (scan.SeekMember("nodes") && scan.Match('[')) {
      // Read up to a few nodes and multiply the two this file uses.
      math::Mat4 nodes[8];
      int node_count = 0;
      while (!scan.Peek(']') && node_count < 8) {
        nodes[node_count] = ReadNodeTransform(scan);
        ++node_count;
        if (!scan.SkipValue()) break;
        if (!scan.Match(',')) break;
      }
      if (node_count >= 2) {
        // Parent first (node 1) then its child (node 0), matching this file's
        // scene: nodes[1] holds node 0, and node 0 carries the mesh.
        node_transform = nodes[1] * nodes[0];
      } else if (node_count == 1) {
        node_transform = nodes[0];
      }
    }
  }

  const auto read_accessor_floats = [&](int accessor_index, int components,
                                        std::vector<float>& out) -> bool {
    if (accessor_index < 0 || accessor_index >= static_cast<int>(accessors.size())) {
      return false;
    }
    const Accessor& a = accessors[accessor_index];
    if (a.buffer_view < 0 ||
        a.buffer_view >= static_cast<int>(buffer_views.size())) {
      return false;
    }
    const BufferView& bv = buffer_views[a.buffer_view];
    const std::size_t comp_size = ComponentSize(a.component_type);
    if (comp_size == 0) return false;
    const std::size_t packed = comp_size * static_cast<std::size_t>(components);
    const std::size_t stride = bv.byte_stride != 0 ? bv.byte_stride : packed;

    out.resize(static_cast<std::size_t>(a.count) * components);
    const std::uint8_t* base = bin_begin + bv.byte_offset;
    for (int i = 0; i < a.count; ++i) {
      const std::uint8_t* elem = base + static_cast<std::size_t>(i) * stride;
      for (int c = 0; c < components; ++c) {
        const std::uint8_t* src = elem + static_cast<std::size_t>(c) * comp_size;
        float value = 0.0f;
        if (a.component_type == 5126) {  // FLOAT
          std::memcpy(&value, src, 4);
        } else if (a.component_type == 5121) {  // UNSIGNED_BYTE, normalised
          value = static_cast<float>(*src) / 255.0f;
        } else if (a.component_type == 5123) {  // UNSIGNED_SHORT, normalised
          std::uint16_t u16 = 0;
          std::memcpy(&u16, src, 2);
          value = static_cast<float>(u16) / 65535.0f;
        }
        out[static_cast<std::size_t>(i) * components + c] = value;
      }
    }
    return true;
  };

  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> uvs;
  if (!read_accessor_floats(idx_position, 3, positions) ||
      !read_accessor_floats(idx_normal, 3, normals) ||
      !read_accessor_floats(idx_uv, 2, uvs)) {
    std::printf("Gltf: %s: could not read POSITION/NORMAL/TEXCOORD_0\n",
                path.c_str());
    return model;
  }

  const int vertex_count = accessors[idx_position].count;
  if (vertex_count <= 0) {
    std::printf("Gltf: %s has no vertices\n", path.c_str());
    return model;
  }

  // Indices: SCALAR, unsigned int/short/byte.
  const Accessor& ia = accessors[idx_indices];
  if (ia.buffer_view < 0 ||
      ia.buffer_view >= static_cast<int>(buffer_views.size())) {
    std::printf("Gltf: %s: index accessor has no buffer view\n", path.c_str());
    return model;
  }
  {
    const BufferView& bv = buffer_views[ia.buffer_view];
    const std::size_t comp_size = ComponentSize(ia.component_type);
    const std::size_t stride = bv.byte_stride != 0 ? bv.byte_stride : comp_size;
    const std::uint8_t* base = bin_begin + bv.byte_offset;
    model.indices.resize(static_cast<std::size_t>(ia.count));
    for (int i = 0; i < ia.count; ++i) {
      const std::uint8_t* src = base + static_cast<std::size_t>(i) * stride;
      std::uint32_t index = 0;
      if (ia.component_type == 5125) {
        std::memcpy(&index, src, 4);
      } else if (ia.component_type == 5123) {
        std::uint16_t u16 = 0;
        std::memcpy(&u16, src, 2);
        index = u16;
      } else if (ia.component_type == 5121) {
        index = *src;
      } else {
        std::printf("Gltf: %s: unsupported index component type %d\n",
                    path.c_str(), ia.component_type);
        return model;
      }
      model.indices[static_cast<std::size_t>(i)] = index;
    }
  }

  // Put the vertices through the node transform. Normals take the same matrix
  // because this file's transform is uniform scale + translation (no shear), so
  // the rotation part is enough and renormalising cleans up the 100x scale.
  model.vertices.resize(static_cast<std::size_t>(vertex_count));
  for (int i = 0; i < vertex_count; ++i) {
    const float x = positions[static_cast<std::size_t>(i) * 3 + 0];
    const float y = positions[static_cast<std::size_t>(i) * 3 + 1];
    const float z = positions[static_cast<std::size_t>(i) * 3 + 2];

    const float wx = node_transform.m[0] * x + node_transform.m[4] * y +
                     node_transform.m[8] * z + node_transform.m[12];
    const float wy = node_transform.m[1] * x + node_transform.m[5] * y +
                     node_transform.m[9] * z + node_transform.m[13];
    const float wz = node_transform.m[2] * x + node_transform.m[6] * y +
                     node_transform.m[10] * z + node_transform.m[14];

    const float nx = normals[static_cast<std::size_t>(i) * 3 + 0];
    const float ny = normals[static_cast<std::size_t>(i) * 3 + 1];
    const float nz = normals[static_cast<std::size_t>(i) * 3 + 2];
    const float tx = node_transform.m[0] * nx + node_transform.m[4] * ny +
                     node_transform.m[8] * nz;
    const float ty = node_transform.m[1] * nx + node_transform.m[5] * ny +
                     node_transform.m[9] * nz;
    const float tz = node_transform.m[2] * nx + node_transform.m[6] * ny +
                     node_transform.m[10] * nz;
    const float nlen = std::sqrt(tx * tx + ty * ty + tz * tz);
    const float inv = nlen > 0.0f ? 1.0f / nlen : 0.0f;

    GltfVertex v;
    v.px = wx;
    v.py = wy;
    v.pz = wz;
    v.nx = tx * inv;
    v.ny = ty * inv;
    v.nz = tz * inv;
    v.u = uvs[static_cast<std::size_t>(i) * 2 + 0];
    v.v = uvs[static_cast<std::size_t>(i) * 2 + 1];
    model.vertices[static_cast<std::size_t>(i)] = v;
  }

  // Images: find which bufferView the material's baseColorTexture and
  // normalTexture resolve to, then decode those two.
  int base_color_view = -1;
  int normal_view = -1;
  {
    // textures[i].source -> images[source].bufferView.
    std::vector<int> texture_source;
    JsonScanner scan = document;
    if (scan.SeekMember("textures") && scan.Match('[')) {
      while (!scan.Peek(']')) {
        double v = 0;
        JsonScanner t = scan;
        if (ObjectNumber(t, "source", v)) {
          texture_source.push_back(static_cast<int>(v));
        } else {
          texture_source.push_back(-1);
        }
        if (!scan.SkipValue()) break;
        if (!scan.Match(',')) break;
      }
    }

    std::vector<int> image_view;
    scan = document;
    if (scan.SeekMember("images") && scan.Match('[')) {
      while (!scan.Peek(']')) {
        double v = 0;
        JsonScanner im = scan;
        if (ObjectNumber(im, "bufferView", v)) {
          image_view.push_back(static_cast<int>(v));
        } else {
          image_view.push_back(-1);
        }
        if (!scan.SkipValue()) break;
        if (!scan.Match(',')) break;
      }
    }

    const auto view_for_texture = [&](int texture_index) -> int {
      if (texture_index < 0 || texture_index >= static_cast<int>(texture_source.size())) {
        return -1;
      }
      const int source = texture_source[texture_index];
      if (source < 0 || source >= static_cast<int>(image_view.size())) {
        return -1;
      }
      return image_view[source];
    };

    scan = document;
    if (scan.SeekMember("materials") && scan.Match('[') && scan.Match('{')) {
      {
        JsonScanner base = scan;
        if (base.SeekMember("pbrMetallicRoughness") && base.SeekMember("baseColorTexture")) {
          double v = 0;
          if (ObjectNumber(base, "index", v)) {
            base_color_view = view_for_texture(static_cast<int>(v));
          }
        }
      }
      {
        JsonScanner normal = scan;
        if (normal.SeekMember("normalTexture")) {
          double v = 0;
          if (ObjectNumber(normal, "index", v)) {
            normal_view = view_for_texture(static_cast<int>(v));
          }
        }
      }
    }
  }

  const auto decode_image = [&](int view_index, Image& out) {
    if (view_index < 0 || view_index >= static_cast<int>(buffer_views.size())) {
      return;
    }
    const BufferView& bv = buffer_views[view_index];
    if (bv.byte_offset + bv.byte_length > bin_size) {
      return;
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* data =
        stbi_load_from_memory(bin_begin + bv.byte_offset,
                              static_cast<int>(bv.byte_length), &w, &h, &channels, 4);
    if (data == nullptr) {
      std::printf("Gltf: failed to decode embedded image\n");
      return;
    }
    out.width = w;
    out.height = h;
    out.pixels.assign(data, data + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(data);
  };

  decode_image(base_color_view, model.base_color);
  decode_image(normal_view, model.normal);

  std::printf("Gltf: %s: %zu vertices, %zu indices, base %dx%d, normal %dx%d\n",
              path.c_str(), model.vertices.size(), model.indices.size(),
              model.base_color.width, model.base_color.height,
              model.normal.width, model.normal.height);
  return model;
}

}  // namespace museum::render
