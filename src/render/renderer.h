// WebGPU renderer: device bring-up, a configurable surface, and the frame.
//
// Batch 1 renders a grid that scrolls with the camera, which is a frame-rate
// and resize check that doubles as proof the view/projection matrices are
// right: the grid must stay locked to the world as you walk and turn.
#pragma once

#include <cstdint>

#include <webgpu/webgpu_cpp.h>

#include "core/math.h"
#include "platform/window.h"
#include "player/player.h"

namespace museum::render {

class Renderer {
 public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Brings up instance -> adapter -> device -> surface. Web and native differ
  // only in how the surface is obtained.
  bool Initialize(platform::Window* window);

  // Called when the window or canvas changed size.
  void Resize(std::uint32_t width, std::uint32_t height);

  // Records and submits one frame.
  void RenderFrame(const player::ViewMatrices& matrices);

  bool ready() const { return ready_; }

 private:
  void BuildPipeline();
  void CreateDepthTarget();
  bool CreateSurface();
  void ConfigureSurface();

  // Blocks until an async WebGPU callback has fired. Web: yields to the
  // browser event loop. Native: pumps Dawn's event queue.
  bool WaitFor(bool& flag);

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

  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  bool ready_ = false;
  bool surface_configured_ = false;
};

}  // namespace museum::render
