#include "render/renderer.h"

#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace museum::render {
namespace {

// A large ground plane drawn as two triangles, with a world-space grid computed
// in the fragment shader. Nothing is uploaded for the grid itself, so this also
// exercises the uniform path (view-projection + camera position) that the real
// museum rooms will use.
//
// The grid fades with distance so the horizon does not alias.
const char kSceneShader[] = R"(
struct Uniforms {
  view_proj : mat4x4<f32>,
  camera_pos : vec4<f32>,
};
@group(0) @binding(0) var<uniform> u : Uniforms;

struct VertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) world_pos : vec3<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) index : u32) -> VertexOut {
  var corners = array<vec2<f32>, 6>(
    vec2<f32>(-1.0, -1.0), vec2<f32>( 1.0, -1.0), vec2<f32>(-1.0,  1.0),
    vec2<f32>(-1.0,  1.0), vec2<f32>( 1.0, -1.0), vec2<f32>( 1.0,  1.0));

  // 300 metre plane, centred on the camera, at y = 0.
  let extent = 300.0;
  let corner = corners[index] * extent;
  let world = vec3<f32>(corner.x + u.camera_pos.x, 0.0, corner.y + u.camera_pos.z);

  var out : VertexOut;
  out.position = u.view_proj * vec4<f32>(world, 1.0);
  out.world_pos = world;
  return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let cell = 2.0;
  let coord = in.world_pos.xz / cell;
  let grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
  let line = min(grid.x, grid.y);
  let coverage = 1.0 - min(line, 1.0);

  let distance = length(in.world_pos.xz - u.camera_pos.xz);
  let fade = 1.0 - smoothstep(20.0, 120.0, distance);

  let floor_color = vec3<f32>(0.10, 0.11, 0.14);
  let line_color = vec3<f32>(0.35, 0.45, 0.65);
  let color = mix(floor_color, line_color, coverage * fade);

  return vec4<f32>(color, 1.0);
}
)";

// Uniforms must match the WGSL struct: 64-byte mat4 then a padded vec4.
struct Uniforms {
  math::Mat4 view_proj;
  float camera_pos[4];
};
static_assert(sizeof(Uniforms) == 80, "uniform layout must match the WGSL struct");

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

bool Renderer::Initialize(platform::Window* window) {
  window_ = window;
  width_ = window->width();
  height_ = window->height();

  instance_ = wgpu::CreateInstance();

  // RequestAdapter/RequestDevice are asynchronous. On the web we bracket the
  // callback chain with emscripten_sleep so Initialize can return a value; on
  // the desktop Dawn's event queue is pumped instead.
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

  ready_ = true;
  std::printf("Renderer: ready, %ux%u\n", width_, height_);
  return true;
}

bool Renderer::WaitFor(bool& flag) {
#ifdef __EMSCRIPTEN__
  // The callback fires from the browser's event loop, so yield to it.
  int spins = 0;
  while (!flag && spins < 2000) {
    emscripten_sleep(1);
    ++spins;
  }
  return flag;
#else
  // Native: drain Dawn's event queue until the callback lands.
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
  // Dawn wants an HWND plus the module instance. SDL hands us the HWND; the
  // module handle comes from SDL as well, so windows.h stays out of the
  // renderer entirely.
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

  wgpu::RenderPipelineDescriptor pipeline_desc{};
  pipeline_desc.layout = pipeline_layout;
  pipeline_desc.vertex.module = module;
  pipeline_desc.vertex.entryPoint = "vs_main";
  pipeline_desc.fragment = &fragment;
  pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
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
  std::printf("Renderer: resized to %ux%u\n", width_, height_);
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
  color.clearValue = {0.07f, 0.08f, 0.10f, 1.0f};

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
    render_pass.SetPipeline(pipeline_);
    render_pass.SetBindGroup(0, uniform_bind_group_);
    render_pass.Draw(6);
    render_pass.End();
  }
  wgpu::CommandBuffer commands = encoder.Finish();
  queue_.Submit(1, &commands);
}

}  // namespace museum::render
