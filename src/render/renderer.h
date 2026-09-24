// WebGPU renderer: device bring-up, a configurable surface, and the frame.
//
// Three passes:
//   1. shadow depth, once per light, each into its own tile of one atlas
//   2. sky dome, filling the background
//   3. the museum, lit by the nearest few lights and shadow-tested
//
// The shadow pass runs every frame even though the museum is static, because
// it is a handful of small draws; if profiling says otherwise, caching it is a
// one-line change since nothing about the geometry moves.
#pragma once

#include <cstdint>
#include <vector>

#include <webgpu/webgpu_cpp.h>

#include "core/math.h"
#include "platform/window.h"
#include "player/player.h"
#include "world/lights.h"
#include "world/museum.h"
#include "world/shadows.h"
#include "world/sky.h"

namespace museum::render {

class Renderer {
 public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  bool Initialize(platform::Window* window, const world::Mesh& museum_mesh,
                  const world::SkyMesh& sky_mesh,
                  const std::vector<world::SpotLight>& lights,
                  const char* assets_dir);

  void Resize(std::uint32_t width, std::uint32_t height);
  void RenderFrame(const player::ViewMatrices& matrices);

  bool ready() const { return ready_; }

 private:
  void BuildScenePipeline();
  void BuildSkyPipeline();
  void BuildShadowPipeline();
  void CreateDepthTarget();
  void CreateShadowAtlas();
  bool CreateSurface();
  void ConfigureSurface();
  bool WaitFor(bool& flag);

  void UploadMesh(const world::Mesh& mesh, wgpu::Buffer& vertex_buffer,
                  wgpu::Buffer& index_buffer, std::uint32_t& index_count);
  void LoadFloorTexture(const char* assets_dir);
  void CreateSamplers();

  // Renders each selected light's depth tile into the atlas.
  void RenderShadowPass();

  // Chooses the lights that affect this frame's viewpoint, nearest first, and
  // writes their uniforms.
  void SelectLights(const math::Vec3& camera_pos);

  platform::Window* window_ = nullptr;

  wgpu::Instance instance_;
  wgpu::Adapter adapter_;
  wgpu::Device device_;
  wgpu::Queue queue_;
  wgpu::Surface surface_;
  wgpu::TextureFormat surface_format_ = wgpu::TextureFormat::BGRA8Unorm;
  wgpu::TextureFormat depth_format_ = wgpu::TextureFormat::Depth24Plus;
  wgpu::TextureFormat shadow_format_ = wgpu::TextureFormat::Depth32Float;

  // Scene pass
  wgpu::RenderPipeline scene_pipeline_;
  wgpu::Buffer scene_uniforms_;
  wgpu::BindGroup scene_bind_group_;
  wgpu::Buffer mesh_vertices_;
  wgpu::Buffer mesh_indices_;
  std::uint32_t mesh_index_count_ = 0;

  // Sky pass
  wgpu::RenderPipeline sky_pipeline_;
  wgpu::Buffer sky_uniforms_;
  wgpu::BindGroup sky_bind_group_;
  wgpu::Buffer sky_vertices_;
  wgpu::Buffer sky_indices_;
  std::uint32_t sky_index_count_ = 0;

  // Shadow pass
  wgpu::RenderPipeline shadow_pipeline_;
  wgpu::Buffer shadow_uniforms_;
  wgpu::BindGroup shadow_bind_group_;
  wgpu::Texture shadow_atlas_;
  wgpu::TextureView shadow_atlas_view_;

  // Textures and samplers
  wgpu::Texture floor_texture_;
  wgpu::TextureView floor_view_;
  wgpu::Sampler floor_sampler_;
  wgpu::Sampler shadow_sampler_;

  wgpu::Texture depth_texture_;
  wgpu::TextureView depth_view_;

  std::vector<world::SpotLight> lights_;
  std::vector<world::ShadowView> shadow_views_;

  // Per-frame light selection, reused to avoid per-frame allocation.
  std::vector<int> selected_lights_;

  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool ready_ = false;
  bool surface_configured_ = false;
};

}  // namespace museum::render
