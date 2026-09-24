#include "render/renderer.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace museum::render {
namespace {

// Flat-ish shading with a fill light, enough to read the architecture. Batch 4
// swaps this shader for the real lighting model.
//
// The colours are authored in linear space to match the JS museum's config
// (walls 0.298, 0, 0.506) so the two builds look the same at this stage.
const char kSceneShader[] = R"(
struct Uniforms {
  view_proj : mat4x4<f32>,
  camera_pos : vec4<f32>,
  wall_color : vec4<f32>,
  floor_color : vec4<f32>,
  light_dir : vec4<f32>,
};
@group(0) @binding(0) var<uniform> u : Uniforms;

struct VertexIn {
  @location(0) position : vec3<f32>,
  @location(1) normal : vec3<f32>,
  @location(2) uv : vec2<f32>,
};

struct VertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) normal : vec3<f32>,
  @location(1) world_pos : vec3<f32>,
  @location(2) uv : vec2<f32>,
};

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
  var out : VertexOut;
  out.position = u.view_proj * vec4<f32>(in.position, 1.0);
  out.normal = in.normal;
  out.world_pos = in.position;
  out.uv = in.uv;
  return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  // Floors are the -Y-facing boxes; detect by normal so both merged ranges can
  // share one draw call without a per-vertex flag.
  let is_floor = select(0.0, 1.0, in.normal.y > 0.5 && in.world_pos.y < 0.01);
  let base = select(u.wall_color.rgb, u.floor_color.rgb, is_floor > 0.5);

  let n = normalize(in.normal);
  // Hemisphere-ish fill: a key light plus a floor bounce, matching the JS
  // museum's baked look closely enough to compare them.
  let key = max(dot(n, normalize(u.light_dir.xyz)), 0.0);
  let ambient = 0.35 + 0.25 * max(n.y, 0.0);
  let lit = base * (ambient + key * 0.9);

  return vec4<f32>(lit, 1.0);
}
)";

// Must match the WGSL struct: mat4 (64) + four padded vec4s (64).
struct Uniforms {
  math::Mat4 view_proj;
  float camera_pos[4];
  float wall_color[4];
  float floor_color[4];
  float light_dir[4];
};
static_assert(sizeof(Uniforms) == 128, "uniform layout must match the WGSL struct");

void LogString(const wgpu::StringView& message) {
  if (message.length) {
    std::printf("%.*s\n", static_cast<int>(message.length), message.data);
  }
}

}  // namespace

Renderer::~Renderer() {
  if (surface_ && surface_configured_) {
    surface_.Unconfigure();
  }
}

bool Renderer::Initialize(platform::Window* window, const world::Mesh& mesh) {
  window_ = window;
  width_ = window->width();
  height_ = window->height();

  instance_ = wgpu::CreateInstance();

  bool adapter_done = false;
  bool adapter_ok = false;
  instance_.RequestAdapter(
      nullptr, wgpu::CallbackMode::AllowSpontaneous,
      [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter,
          wgpu::StringView message) {
        LogString(message);
        if (status == wgpu::RequestAdapterStatus::Success) {
          adapter_ = adapter;
          adapter_ok = true;
        }
        adapter_done = true;
      });

  if (!WaitFor(adapter_done)) {
    std::printf("Renderer: RequestAdapter timed out\n");
    return false;
  }
  if (!adapter_ok) {
    std::printf("Renderer: no WebGPU adapter (is WebGPU enabled?)\n");
    return false;
  }

  bool device_done = false;
  bool device_ok = false;
  wgpu::DeviceDescriptor device_desc{};
  device_desc.SetUncapturedErrorCallback(
      [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) {
        std::printf("UncapturedError (%d): ", static_cast<int>(type));
        LogString(message);
      });
  adapter_.RequestDevice(
      &device_desc, wgpu::CallbackMode::AllowSpontaneous,
      [&](wgpu::RequestDeviceStatus status, wgpu::Device device,
          wgpu::StringView message) {
        LogString(message);
        if (status == wgpu::RequestDeviceStatus::Success) {
          device_ = device;
          device_ok = true;
        }
        device_done = true;
      });

  if (!WaitFor(device_done) || !device_ok) {
    std::printf("Renderer: RequestDevice failed\n");
    return false;
  }

  queue_ = device_.GetQueue();

  if (!CreateSurface()) {
    return false;
  }

  ConfigureSurface();
  BuildPipeline();
  CreateDepthTarget();
  UploadMesh(mesh);

  ready_ = true;
  std::printf("Renderer: ready, %ux%u, %u indices\n", width_, height_, index_count_);
  return true;
}

bool Renderer::WaitFor(bool& flag) {
#ifdef __EMSCRIPTEN__
  int spins = 0;
  while (!flag && spins < 4000) {
    emscripten_sleep(1);
    ++spins;
  }
  return flag;
#else
  int spins = 0;
  while (!flag && spins < 100000) {
    instance_.ProcessEvents();
    ++spins;
  }
  return flag;
#endif
}

bool Renderer::CreateSurface() {
#ifdef __EMSCRIPTEN__
  wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
  canvas.selector = "#canvas";
  wgpu::SurfaceDescriptor surface_desc{};
  surface_desc.nextInChain = &canvas;
  surface_ = instance_.CreateSurface(&surface_desc);
#else
  wgpu::SurfaceSourceWindowsHWND hwnd{};
  hwnd.hinstance = window_->native_display_handle();
  hwnd.hwnd = window_->native_window_handle();
  wgpu::SurfaceDescriptor surface_desc{};
  surface_desc.nextInChain = &hwnd;
  surface_ = instance_.CreateSurface(&surface_desc);
#endif
  if (!surface_) {
    std::printf("Renderer: CreateSurface failed\n");
    return false;
  }
  return true;
}

void Renderer::ConfigureSurface() {
  wgpu::SurfaceCapabilities caps{};
  surface_.GetCapabilities(adapter_, &caps);
  if (caps.formatCount > 0) {
    surface_format_ = caps.formats[0];
  }

  wgpu::SurfaceConfiguration config{};
  config.device = device_;
  config.format = surface_format_;
  config.usage = wgpu::TextureUsage::RenderAttachment;
  config.width = width_;
  config.height = height_;
  config.alphaMode = wgpu::CompositeAlphaMode::Auto;
  config.presentMode = wgpu::PresentMode::Fifo;
  surface_.Configure(&config);
  surface_configured_ = true;
}

void Renderer::BuildPipeline() {
  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = kSceneShader;

  wgpu::ShaderModuleDescriptor module_desc{};
  module_desc.nextInChain = &wgsl;
  wgpu::ShaderModule module = device_.CreateShaderModule(&module_desc);

  wgpu::BindGroupLayoutEntry uniform_entry{};
  uniform_entry.binding = 0;
  uniform_entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  uniform_entry.buffer.type = wgpu::BufferBindingType::Uniform;
  uniform_entry.buffer.minBindingSize = sizeof(Uniforms);

  wgpu::BindGroupLayoutDescriptor layout_desc{};
  layout_desc.entryCount = 1;
  layout_desc.entries = &uniform_entry;
  wgpu::BindGroupLayout bind_group_layout = device_.CreateBindGroupLayout(&layout_desc);

  wgpu::PipelineLayoutDescriptor pipeline_layout_desc{};
  pipeline_layout_desc.bindGroupLayoutCount = 1;
  pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;
  wgpu::PipelineLayout pipeline_layout = device_.CreatePipelineLayout(&pipeline_layout_desc);

  wgpu::ColorTargetState color_target{};
  color_target.format = surface_format_;

  wgpu::FragmentState fragment{};
  fragment.module = module;
  fragment.entryPoint = "fs_main";
  fragment.targetCount = 1;
  fragment.targets = &color_target;

  wgpu::DepthStencilState depth_stencil{};
  depth_stencil.format = depth_format_;
  depth_stencil.depthWriteEnabled = true;
  depth_stencil.depthCompare = wgpu::CompareFunction::Less;

  wgpu::VertexAttribute attributes[3] = {};
  attributes[0].shaderLocation = 0;
  attributes[0].format = wgpu::VertexFormat::Float32x3;
  attributes[0].offset = offsetof(world::Vertex, px);
  attributes[1].shaderLocation = 1;
  attributes[1].format = wgpu::VertexFormat::Float32x3;
  attributes[1].offset = offsetof(world::Vertex, nx);
  attributes[2].shaderLocation = 2;
  attributes[2].format = wgpu::VertexFormat::Float32x2;
  attributes[2].offset = offsetof(world::Vertex, u);

  wgpu::VertexBufferLayout vertex_layout{};
  vertex_layout.arrayStride = sizeof(world::Vertex);
  vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
  vertex_layout.attributeCount = 3;
  vertex_layout.attributes = attributes;

  wgpu::RenderPipelineDescriptor pipeline_desc{};
  pipeline_desc.layout = pipeline_layout;
  pipeline_desc.vertex.module = module;
  pipeline_desc.vertex.entryPoint = "vs_main";
  pipeline_desc.vertex.bufferCount = 1;
  pipeline_desc.vertex.buffers = &vertex_layout;
  pipeline_desc.fragment = &fragment;
  pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  // The museum is viewed from inside, so no face culling: a culled back face
  // here would punch a hole in the wall behind the player.
  pipeline_desc.primitive.cullMode = wgpu::CullMode::None;
  pipeline_desc.depthStencil = &depth_stencil;
  pipeline_ = device_.CreateRenderPipeline(&pipeline_desc);

  wgpu::BufferDescriptor buffer_desc{};
  buffer_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  buffer_desc.size = sizeof(Uniforms);
  uniform_buffer_ = device_.CreateBuffer(&buffer_desc);

  wgpu::BindGroupEntry bind_entry{};
  bind_entry.binding = 0;
  bind_entry.buffer = uniform_buffer_;
  bind_entry.offset = 0;
  bind_entry.size = sizeof(Uniforms);

  wgpu::BindGroupDescriptor bind_group_desc{};
  bind_group_desc.layout = bind_group_layout;
  bind_group_desc.entryCount = 1;
  bind_group_desc.entries = &bind_entry;
  uniform_bind_group_ = device_.CreateBindGroup(&bind_group_desc);
}

void Renderer::UploadMesh(const world::Mesh& mesh) {
  if (mesh.vertices.empty() || mesh.indices.empty()) {
    return;
  }

  const std::uint64_t vertex_bytes =
      static_cast<std::uint64_t>(mesh.vertices.size()) * sizeof(world::Vertex);
  const std::uint64_t index_bytes =
      static_cast<std::uint64_t>(mesh.indices.size()) * sizeof(std::uint32_t);

  wgpu::BufferDescriptor vertex_desc{};
  vertex_desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
  vertex_desc.size = vertex_bytes;
  vertex_buffer_ = device_.CreateBuffer(&vertex_desc);
  queue_.WriteBuffer(vertex_buffer_, 0, mesh.vertices.data(), vertex_bytes);

  wgpu::BufferDescriptor index_desc{};
  index_desc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
  index_desc.size = index_bytes;
  index_buffer_ = device_.CreateBuffer(&index_desc);
  queue_.WriteBuffer(index_buffer_, 0, mesh.indices.data(), index_bytes);

  index_count_ = static_cast<std::uint32_t>(mesh.indices.size());
}

void Renderer::CreateDepthTarget() {
  wgpu::TextureDescriptor depth_desc{};
  depth_desc.size = {width_, height_, 1};
  depth_desc.format = depth_format_;
  depth_desc.usage = wgpu::TextureUsage::RenderAttachment;
  depth_texture_ = device_.CreateTexture(&depth_desc);
  depth_view_ = depth_texture_.CreateView();
}

void Renderer::Resize(std::uint32_t width, std::uint32_t height) {
  if (!ready_ || (width == width_ && height == height_)) {
    width_ = width;
    height_ = height;
    return;
  }
  width_ = width;
  height_ = height;
  ConfigureSurface();
  CreateDepthTarget();
}

void Renderer::RenderFrame(const player::ViewMatrices& matrices) {
  if (!ready_) {
    return;
  }

  Uniforms uniforms{};
  uniforms.view_proj = matrices.view_projection;
  uniforms.camera_pos[0] = matrices.camera_position.x;
  uniforms.camera_pos[1] = matrices.camera_position.y;
  uniforms.camera_pos[2] = matrices.camera_position.z;
  uniforms.camera_pos[3] = 1.0f;

  // Linear-space colours from the JS museum's config.ts.
  uniforms.wall_color[0] = 0.298f;
  uniforms.wall_color[1] = 0.0f;
  uniforms.wall_color[2] = 0.506f;
  uniforms.wall_color[3] = 1.0f;
  uniforms.floor_color[0] = 0.8f;
  uniforms.floor_color[1] = 0.8f;
  uniforms.floor_color[2] = 0.8f;
  uniforms.floor_color[3] = 1.0f;

  // Key light, angled down and to one side.
  uniforms.light_dir[0] = 0.4f;
  uniforms.light_dir[1] = 0.85f;
  uniforms.light_dir[2] = 0.35f;
  uniforms.light_dir[3] = 0.0f;

  queue_.WriteBuffer(uniform_buffer_, 0, &uniforms, sizeof(uniforms));

  wgpu::SurfaceTexture surface_texture{};
  surface_.GetCurrentTexture(&surface_texture);
  if (surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
      surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
    return;
  }
  wgpu::TextureView view = surface_texture.texture.CreateView();

  wgpu::RenderPassColorAttachment color{};
  color.view = view;
  color.resolveTarget = nullptr;
  color.loadOp = wgpu::LoadOp::Clear;
  color.storeOp = wgpu::StoreOp::Store;
  color.clearValue = {0.05f, 0.06f, 0.08f, 1.0f};

  wgpu::RenderPassDepthStencilAttachment depth{};
  depth.view = depth_view_;
  depth.depthLoadOp = wgpu::LoadOp::Clear;
  depth.depthStoreOp = wgpu::StoreOp::Store;
  depth.depthClearValue = 1.0f;

  wgpu::RenderPassDescriptor pass{};
  pass.colorAttachmentCount = 1;
  pass.colorAttachments = &color;
  pass.depthStencilAttachment = &depth;

  wgpu::CommandEncoder encoder = device_.CreateCommandEncoder();
  {
    wgpu::RenderPassEncoder render_pass = encoder.BeginRenderPass(&pass);
    if (index_count_ > 0) {
      render_pass.SetPipeline(pipeline_);
      render_pass.SetBindGroup(0, uniform_bind_group_);
      render_pass.SetVertexBuffer(0, vertex_buffer_);
      render_pass.SetIndexBuffer(index_buffer_, wgpu::IndexFormat::Uint32, 0,
                                 index_count_ * sizeof(std::uint32_t));
      render_pass.DrawIndexed(index_count_);
    }
    render_pass.End();
  }
  wgpu::CommandBuffer commands = encoder.Finish();
  queue_.Submit(1, &commands);
}

}  // namespace museum::render
