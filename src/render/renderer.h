// WebGPU renderer: device bring-up, a configurable surface, and the frame.
//
// Batch 2 renders the museum's merged static geometry with a simple directional
// fill light. The real lighting model arrives in Batch 4; for now the goal is
// that the rooms read correctly in perspective and the collider set matches
// what is drawn.
#pragma once

#include <cstdint>

#include <webgpu/webgpu_cpp.h>

#include "core/math.h"
#include "platform/window.h"
#include "player/player.h"
#include "world/museum.h"

namespace museum::render {

class Renderer {
 public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  bool Initialize(platform::Window* window, const world::Mesh& mesh);

  void Resize(std::uint32_t width, std::uint32_t height);
  void RenderFrame(const player::ViewMatrices& matrices);

  bool ready() const { return ready_; }

 private:
  void BuildPipeline();
  void CreateDepthTarget();
  bool CreateSurface();
  void ConfigureSurface();
  bool WaitFor(bool& flag);

  void UploadMesh(const world::Mesh& mesh);

  platform::Window* window_ = nullptr;

  wgpu::Instance instance_;
  wgpu::Adapter adapter_;
  wgpu::Device device_;
  wgpu::Queue queue_;
  wgpu::Surface surface_;
  wgpu::TextureFormat surface_format_ = wgpu::TextureFormat::BGRA8Unorm;
  wgpu::TextureFormat depth_format_ = wgpu::TextureFormat::Depth24Plus;

  wgpu::RenderPipeline pipeline_;
  wgpu::Buffer uniform_buffer_;
  wgpu::BindGroup uniform_bind_group_;
  wgpu::Texture depth_texture_;
  wgpu::TextureView depth_view_;

  wgpu::Buffer vertex_buffer_;
  wgpu::Buffer index_buffer_;
  std::uint32_t index_count_ = 0;

  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool ready_ = false;
  bool surface_configured_ = false;
};

}  // namespace museum::render
