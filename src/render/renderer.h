// WebGPU renderer: device bring-up, a configurable surface, and the frame.
//
// Draw order is sky first (a full-screen dome with no depth write), then the
// museum with depth testing. Both passes share the same exposure, tonemap and
// sRGB encode, so the dome and the walls meet seamlessly at the horizon.
#pragma once

#include <cstdint>

#include <webgpu/webgpu_cpp.h>

#include "core/math.h"
#include "platform/window.h"
#include "player/player.h"
#include "world/museum.h"
#include "world/sky.h"

namespace museum::render {

class Renderer {
 public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // `assets_dir` is where textures live: "assets" on the web (packaged into the
  // Emscripten filesystem) or a path on disk natively.
  bool Initialize(platform::Window* window, const world::Mesh& museum_mesh,
                  const world::SkyMesh& sky_mesh, const char* assets_dir);

  void Resize(std::uint32_t width, std::uint32_t height);
  void RenderFrame(const player::ViewMatrices& matrices);

  bool ready() const { return ready_; }

 private:
  void BuildScenePipeline();
  void BuildSkyPipeline();
  void CreateDepthTarget();
  bool CreateSurface();
  void ConfigureSurface();
  bool WaitFor(bool& flag);

  void UploadMesh(const world::Mesh& mesh, wgpu::Buffer& vertex_buffer,
                  wgpu::Buffer& index_buffer, std::uint32_t& index_count);
  void LoadFloorTexture(const char* assets_dir);
  void CreateSampler();

  platform::Window* window_ = nullptr;

  wgpu::Instance instance_;
  wgpu::Adapter adapter_;
  wgpu::Device device_;
  wgpu::Queue queue_;
  wgpu::Surface surface_;
  wgpu::TextureFormat surface_format_ = wgpu::TextureFormat::BGRA8Unorm;
  wgpu::TextureFormat depth_format_ = wgpu::TextureFormat::Depth24Plus;

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

  // Textures
  wgpu::Texture floor_texture_;
  wgpu::TextureView floor_view_;
  wgpu::Sampler floor_sampler_;

  wgpu::Texture depth_texture_;
  wgpu::TextureView depth_view_;

  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool ready_ = false;
  bool surface_configured_ = false;
};

}  // namespace museum::render
