#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "render/gltf.h"
#include "render/painting_atlas.h"
#include "world/config.h"

namespace museum {
namespace {

// Where the sculpture stands, and how tall it is. The JS museum never used the
// scan, so there is no reference placement to port: this is a design choice.
// The spawn room is the one the player wakes in (room centre (0, -10) on the
// first floor) and the figure is sized to read as a statue rather than a
// monument, so the 2 m tall target keeps its base on the floor and its head
// well under the 6 m ceiling.
constexpr float kSculptureHeight = 2.0f;
constexpr float kSculptureCentreX = 0.0f;
constexpr float kSculptureCentreZ = -10.0f;

// Turns the loaded glTF into museum geometry: uniform scale to the target
// height, recentred on X/Z, base dropped onto the floor, then a yaw so the
// figure faces the spawn point. Returns an empty SculptureMesh when the model
// could not be read, which the museum treats as "no sculpture".
world::SculptureMesh PlaceSculpture(const render::GltfModel& model) {
  world::SculptureMesh out;
  if (model.empty()) {
    return out;
  }

  // The scan arrives in model space (the node transform is uniform, so this is
  // still a scaled copy of the mesh). Find its bounds to derive the placement.
  float min_x = model.vertices[0].px;
  float max_x = model.vertices[0].px;
  float min_y = model.vertices[0].py;
  float max_y = model.vertices[0].py;
  float min_z = model.vertices[0].pz;
  float max_z = model.vertices[0].pz;
  for (const render::GltfVertex& v : model.vertices) {
    min_x = std::min(min_x, v.px);
    max_x = std::max(max_x, v.px);
    min_y = std::min(min_y, v.py);
    max_y = std::max(max_y, v.py);
    min_z = std::min(min_z, v.pz);
    max_z = std::max(max_z, v.pz);
  }

  const float source_height = max_y - min_y;
  const float scale = source_height > 0.0f ? kSculptureHeight / source_height : 1.0f;

  // Yaw so the scanned figure's front (+Z) turns toward the spawn point.
  const float to_spawn_x = player::kSpawnX - kSculptureCentreX;
  const float to_spawn_z = player::kSpawnZ - kSculptureCentreZ;
  const float yaw = std::atan2(to_spawn_x, to_spawn_z);
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);

  const float centre_source_x = (min_x + max_x) * 0.5f;
  const float centre_source_z = (min_z + max_z) * 0.5f;
  const float base_y = config::FloorTopY(0);

  out.vertices.reserve(model.vertices.size());
  for (const render::GltfVertex& v : model.vertices) {
    // Centre on X/Z, put the feet on the floor.
    const float lx = (v.px - centre_source_x) * scale;
    const float ly = (v.py - min_y) * scale;
    const float lz = (v.pz - centre_source_z) * scale;

    world::Vertex vertex;
    vertex.px = kSculptureCentreX + lx * cy + lz * sy;
    vertex.py = base_y + ly;
    vertex.pz = kSculptureCentreZ - lx * sy + lz * cy;
    vertex.nx = v.nx * cy + v.nz * sy;
    vertex.ny = v.ny;
    vertex.nz = -v.nx * sy + v.nz * cy;
    vertex.u = v.u;
    vertex.v = v.v;
    out.vertices.push_back(vertex);
  }
  out.indices = model.indices;

  // A box collider around the statue's footprint, slightly inset so the player
  // can brush past without snagging on the scan's own detail.
  const float width = (max_x - min_x) * scale;
  const float depth = (max_z - min_z) * scale;
  out.collider_cx = kSculptureCentreX;
  out.collider_cy = base_y + kSculptureHeight * 0.5f;
  out.collider_cz = kSculptureCentreZ;
  out.collider_sx = width * 0.8f;
  out.collider_sy = kSculptureHeight;
  out.collider_sz = depth * 0.8f;

  std::printf("Sculpture: placed at (%.2f, %.2f), %.2f m tall\n",
              kSculptureCentreX, kSculptureCentreZ, kSculptureHeight);
  return out;
}

}  // namespace

bool Engine::Initialize(const char* title, std::uint32_t width, std::uint32_t height) {
  museum_.Build();
  const world::MuseumStats& stats = museum_.stats();
  std::printf(
      "Museum: %d rooms, %d segments, %d colliders, %d painting spots, "
      "y %.1f..%.1f\n",
      stats.rooms, stats.segments, stats.colliders, stats.painting_spots,
      stats.min_y, stats.max_y);

  // The paintings atlas is built before the mesh, because the mesh needs each
  // painting's rectangle to write atlas UVs into the canvas vertices. Decoding
  // 27 JPEGs is the slowest part of startup.
  const render::PaintingAtlas paintings =
      render::BuildPaintingAtlas(std::string(kAssetsDir) + "/paintings", "jpg");

  std::vector<world::Museum::PaintingUv> painting_uvs;
  painting_uvs.reserve(paintings.entries.size());
  for (const render::PaintingAtlasEntry& e : paintings.entries) {
    painting_uvs.push_back({e.u0, e.v0, e.u1, e.v1, e.aspect});
  }

  // The sculpture is optional: if it fails to parse, the museum still builds
  // with an empty SculptureMesh and simply has no statue.
  const render::GltfModel sculpture_model = render::LoadGlb(
      std::string(kAssetsDir) + "/models/Untitled.glb");
  const world::SculptureMesh sculpture = PlaceSculpture(sculpture_model);

  world::Mesh mesh;
  museum_.EmitMesh(mesh, painting_uvs, sculpture);
  std::printf("Museum: mesh %zu vertices, %zu indices (%u painting, %u sculpture)\n",
              mesh.vertices.size(), mesh.indices.size(),
              mesh.painting_index_count, mesh.sculpture_index_count);

  const world::SkyMesh sky = world::BuildSkyDome();

  if (!window_.Create(title, width, height)) {
    return false;
  }

  player_.SetWorld(&museum_.collision());

  if (!renderer_.Initialize(&window_, mesh, sky, museum_.lights(), kAssetsDir,
                            paintings.image, sculpture_model.base_color,
                            sculpture_model.normal)) {
    return false;
  }

  initialized_ = true;
  return true;
}

void Engine::HandleResize() {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  if (window_.ConsumeResize(&width, &height)) {
    renderer_.Resize(width, height);
  }
}

void Engine::Frame() {
  if (!initialized_) {
    return;
  }

  window_.PollEvents();
  HandleResize();

  const float dt = clock_.Tick();

  math::MoveInput move;
  move.forward = window_.input().move_forward;
  move.strafe = window_.input().move_strafe;

  // A held look stick is a rotation rate, but the look path is expressed in
  // pixel deltas (the same units a mouse reports), so convert using dt. At full
  // deflection this turns at kLookStickSpeed radians per second.
  //
  // The sign is worth spelling out, because it is easy to get backwards twice.
  // ApplyLook does `yaw_ -= dx`, so the value written here is subtracted, not
  // added. The stick's x is screen-right positive, and a right push must turn
  // right, which is yaw decreasing. yaw_ -= look_yaw, so for yaw to fall on a
  // positive x, look_yaw must be positive: hence `+= x`, not `+= -x`.
  platform::InputState& input = window_.mutable_input();
  if (input.look_stick_active) {
    const float pixels = player::kLookStickSpeed * dt;
    input.look_yaw += input.look_stick_x * pixels;
    // y is screen-up positive in the stick but ApplyLook wants screen-down
    // positive, and it subtracts, so this one does negate.
    input.look_pitch += -input.look_stick_y * pixels;
  }

  player_.Update(dt, move, window_.input());

  const float aspect = window_.height() > 0
                           ? static_cast<float>(window_.width()) /
                                 static_cast<float>(window_.height())
                           : 1.0f;
  renderer_.RenderFrame(player_.BuildViewMatrices(aspect));

  window_.EndFrame();
}

}  // namespace museum
