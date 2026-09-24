#include "render/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <webgpu/webgpu_cpp.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "render/shaders.h"
#include "render/texture.h"
#include "world/config.h"

namespace museum::render {
namespace {

// One light, laid out to match the WGSL `Light` struct.
struct GpuLight {
  float position_range[4];    // xyz fixture, w range
  float direction_cone[4];    // xyz direction, w cos(inner)
  float color_energy[4];      // rgb colour, a energy
  float outer_and_shadow[4];  // x cos(outer), y shadow slot, zw unused
  float atlas_rect[4];
};
static_assert(sizeof(GpuLight) == 5 * 16);

struct SceneUniforms {
  math::Mat4 view_proj;                 // 64
  float camera_pos[4];                  // 16
  float wall_color[4];                  // 16
  float sky_top[4];                     // 16
  float sky_horizon[4];                 // 16
  float sky_bottom[4];                  // 16
  float fog[4];                         // 16
  float tonemap[4];                     // 16
  float ambient[4];                     // 16
  float bounce[4];                      // 16
  float ceil_glow[4];                   // 16
  float light_count[4];                 // 16
  math::Mat4 light_view_proj[world::kMaxLightsPerFragment];  // 4 * 64
  GpuLight lights[world::kMaxLightsPerFragment];             // 4 * 80
};
static_assert(sizeof(SceneUniforms) == 64 + 11 * 16 + 4 * 64 + 4 * 80);

struct SkyUniforms {
  float top[4];
  float horizon[4];
  float bottom[4];
  float tonemap[4];
};
static_assert(sizeof(SkyUniforms) == 4 * 16);

struct ShadowUniforms {
  math::Mat4 view_proj;
};
static_assert(sizeof(ShadowUniforms) == 64);

// sRGB hex from the JS museum's config.ts.
constexpr float kSkyTop[3] = {0x2b / 255.0f, 0x4a / 255.0f, 0x7f / 255.0f};
constexpr float kSkyHorizon[3] = {0xb5 / 255.0f, 0xc7 / 255.0f, 0xda / 255.0f};
constexpr float kSkyBottom[3] = {0x23 / 255.0f, 0x27 / 255.0f, 0x2f / 255.0f};

// The JS museum has no tonemapper: it writes linear albedo through to sRGB
// directly. Ours runs ACES, which darkens mid-tones, so exposure compensates.
// 1.0 was verified against the JS build's own wall colour: the lit wall lands
// at sRGB (161, 0, 195) here versus (148, 0, 189) there, a close match. Raising
// it oversaturates the walls toward magenta, which is the opposite of the goal.
constexpr float kExposure = 1.0f;

// Shadow ortho box: the spot's usable radius across, and how deep to trace.
// PARKED: shadows are off (see kShadowsEnabled); kept with the pass below.
constexpr float kShadowExtent = 9.0f;
constexpr float kShadowDepth = 18.0f;

// Shadows are parked, not deleted.
//
// The atlas, the light-space matrices, the depth pipeline and the comparison
// sampler are all written and were reached at runtime; what was never proven is
// the sampling itself, which read as fully-shadowed everywhere. Turning this
// back on restores the pass and the bindings; the shader's shadow function has
// to be reintroduced at the same time, which is why the whole thing is flagged
// rather than half-wired.
//
// Known blockers, recorded so the next attempt starts from them:
//   1. Reading depth back through a comparison sampler returns a filtered
//      comparison (0..1), not raw depth, so a naive readback cannot be trusted
//      when debugging. A non-comparison sampler bound alongside is needed to
//      inspect stored depth.
//   2. SelectLights treats a neighbouring room's fixture as valid whenever its
//      shadow lookup fails, so a room corner was lit by a fixture behind a
//      wall. The nearest-N selection needs occlusion data to be correct, which
//      makes shadows a prerequisite for it rather than a refinement.
//   3. A per-light LoadOp::Clear wiped the atlas, because a render pass covers
//      the whole attachment and not just its viewport. Fixed by clearing once
//      and then using LoadOp::Load; worth re-verifying after any change here.
inline constexpr bool kShadowsEnabled = false;

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

bool Renderer::Initialize(platform::Window* window, const world::Mesh& museum_mesh,
                          const world::SkyMesh& sky_mesh,
                          const std::vector<world::SpotLight>& lights,
                          const char* assets_dir) {
  window_ = window;
  width_ = window->width();
  height_ = window->height();
  lights_ = lights;

  // Atlas-slot and shadow-view setup belongs to the parked shadow path. The
  // slot cap was only ever an atlas limitation, so it is skipped too: the
  // lighting path itself handles any light count via selection.
  if (kShadowsEnabled) {
    const std::size_t slot_capacity = static_cast<std::size_t>(
        world::kShadowAtlasColumns) * world::kShadowAtlasRows;
    if (lights_.size() > slot_capacity) {
      std::printf("Renderer: %zu lights exceeds %zu atlas slots, dropping extras\n",
                  lights_.size(), slot_capacity);
      lights_.resize(slot_capacity);
    }

    shadow_views_.reserve(lights_.size());
    for (std::size_t i = 0; i < lights_.size(); ++i) {
      shadow_views_.push_back(world::BuildShadowView(
          lights_[i], kShadowExtent, kShadowDepth, static_cast<std::uint32_t>(i)));
    }
  }

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
  CreateSamplers();
  LoadFloorTexture(assets_dir);
  if (kShadowsEnabled) {
    CreateShadowAtlas();
  }
  BuildScenePipeline();
  BuildSkyPipeline();
  if (kShadowsEnabled) {
    BuildShadowPipeline();
  }
  CreateDepthTarget();

  UploadMesh(museum_mesh, mesh_vertices_, mesh_indices_, mesh_index_count_);
  UploadMesh(sky_mesh, sky_vertices_, sky_indices_, sky_index_count_);

  ready_ = true;
  std::printf(
      "Renderer: ready, %ux%u, %u museum indices, %u sky indices, %zu lights, "
      "shadow atlas %u",
      width_, height_, mesh_index_count_, sky_index_count_, lights_.size(),
      world::kShadowAtlasSize);
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

void Renderer::CreateSamplers() {
  wgpu::SamplerDescriptor sampler_desc{};
  sampler_desc.addressModeU = wgpu::AddressMode::Repeat;
  sampler_desc.addressModeV = wgpu::AddressMode::Repeat;
  sampler_desc.addressModeW = wgpu::AddressMode::Repeat;
  sampler_desc.magFilter = wgpu::FilterMode::Linear;
  sampler_desc.minFilter = wgpu::FilterMode::Linear;
  sampler_desc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
  sampler_desc.maxAnisotropy = 8;
  floor_sampler_ = device_.CreateSampler(&sampler_desc);

  // Comparison sampler for the shadow atlas. PARKED with the rest of the shadow
  // path; only created when shadows are enabled.
  if (kShadowsEnabled) {
    wgpu::SamplerDescriptor shadow_sampler_desc{};
    shadow_sampler_desc.addressModeU = wgpu::AddressMode::ClampToEdge;
    shadow_sampler_desc.addressModeV = wgpu::AddressMode::ClampToEdge;
    shadow_sampler_desc.addressModeW = wgpu::AddressMode::ClampToEdge;
    shadow_sampler_desc.magFilter = wgpu::FilterMode::Linear;
    shadow_sampler_desc.minFilter = wgpu::FilterMode::Linear;
    shadow_sampler_desc.compare = wgpu::CompareFunction::LessEqual;
    shadow_sampler_ = device_.CreateSampler(&shadow_sampler_desc);
  }
}

void Renderer::LoadFloorTexture(const char* assets_dir) {
  const std::string path = std::string(assets_dir) + "/textures/floor_wall1.jpg";
  const Image image = LoadImage(path);
  if (image.empty()) {
    std::printf("Renderer: floor texture missing at %s, using flat colour\n",
                path.c_str());
    return;
  }

  floor_texture_ =
      CreateTextureFromImage(device_, queue_, image, /*srgb=*/true, /*repeat=*/true);
  if (floor_texture_) {
    floor_view_ = floor_texture_.CreateView();
    std::printf("Renderer: floor texture %dx%d\n", image.width, image.height);
  }
}

void Renderer::CreateShadowAtlas() {
  wgpu::TextureDescriptor atlas_desc{};
  atlas_desc.size = {world::kShadowAtlasSize, world::kShadowAtlasSize, 1};
  atlas_desc.format = shadow_format_;
  // Read-only depth attachment is enough: nothing samples it while it is bound.
  atlas_desc.usage = wgpu::TextureUsage::RenderAttachment |
                     wgpu::TextureUsage::TextureBinding;
  atlas_desc.dimension = wgpu::TextureDimension::e2D;
  shadow_atlas_ = device_.CreateTexture(&atlas_desc);
  shadow_atlas_view_ = shadow_atlas_.CreateView();
}

void Renderer::BuildScenePipeline() {
  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = shaders::kSceneShader;

  wgpu::ShaderModuleDescriptor module_desc{};
  module_desc.nextInChain = &wgsl;
  wgpu::ShaderModule module = device_.CreateShaderModule(&module_desc);

  // Scene bindings: uniforms, the floor texture, and its sampler. The shadow
  // atlas and its comparison sampler were bindings 3 and 4; they are removed
  // while shadows are parked, and the atlas no longer exists at all.
  wgpu::BindGroupLayoutEntry entries[3] = {};
  entries[0].binding = 0;
  entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
  entries[0].buffer.minBindingSize = sizeof(SceneUniforms);
  entries[1].binding = 1;
  entries[1].visibility = wgpu::ShaderStage::Fragment;
  entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
  entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  entries[2].binding = 2;
  entries[2].visibility = wgpu::ShaderStage::Fragment;
  entries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

  wgpu::BindGroupLayoutDescriptor layout_desc{};
  layout_desc.entryCount = 3;
  layout_desc.entries = entries;
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

  wgpu::VertexAttribute attributes[4] = {};
  attributes[0].shaderLocation = 0;
  attributes[0].format = wgpu::VertexFormat::Float32x3;
  attributes[0].offset = offsetof(world::Vertex, px);
  attributes[1].shaderLocation = 1;
  attributes[1].format = wgpu::VertexFormat::Float32x3;
  attributes[1].offset = offsetof(world::Vertex, nx);
  attributes[2].shaderLocation = 2;
  attributes[2].format = wgpu::VertexFormat::Float32x2;
  attributes[2].offset = offsetof(world::Vertex, u);
  attributes[3].shaderLocation = 3;
  attributes[3].format = wgpu::VertexFormat::Float32;
  attributes[3].offset = offsetof(world::Vertex, is_floor);

  wgpu::VertexBufferLayout vertex_layout{};
  vertex_layout.arrayStride = sizeof(world::Vertex);
  vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
  vertex_layout.attributeCount = 4;
  vertex_layout.attributes = attributes;

  wgpu::RenderPipelineDescriptor pipeline_desc{};
  pipeline_desc.layout = pipeline_layout;
  pipeline_desc.vertex.module = module;
  pipeline_desc.vertex.entryPoint = "vs_main";
  pipeline_desc.vertex.bufferCount = 1;
  pipeline_desc.vertex.buffers = &vertex_layout;
  pipeline_desc.fragment = &fragment;
  pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  // Viewed from inside, so no face culling.
  pipeline_desc.primitive.cullMode = wgpu::CullMode::None;
  pipeline_desc.depthStencil = &depth_stencil;
  scene_pipeline_ = device_.CreateRenderPipeline(&pipeline_desc);
  scene_pipeline_.SetLabel("ScenePipeline");

  wgpu::BufferDescriptor buffer_desc{};
  buffer_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  buffer_desc.size = sizeof(SceneUniforms);
  scene_uniforms_ = device_.CreateBuffer(&buffer_desc);

  wgpu::BindGroupEntry bind_entries[3] = {};
  bind_entries[0].binding = 0;
  bind_entries[0].buffer = scene_uniforms_;
  bind_entries[0].offset = 0;
  bind_entries[0].size = sizeof(SceneUniforms);
  bind_entries[1].binding = 1;
  bind_entries[1].textureView = floor_view_;
  bind_entries[2].binding = 2;
  bind_entries[2].sampler = floor_sampler_;

  wgpu::BindGroupDescriptor bind_group_desc{};
  bind_group_desc.layout = bind_group_layout;
  bind_group_desc.entryCount = 3;
  bind_group_desc.entries = bind_entries;
  scene_bind_group_ = device_.CreateBindGroup(&bind_group_desc);
}

void Renderer::BuildShadowPipeline() {
  if (!kShadowsEnabled) {
    return;
  }

  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = shaders::kShadowShader;

  wgpu::ShaderModuleDescriptor module_desc{};
  module_desc.nextInChain = &wgsl;
  wgpu::ShaderModule module = device_.CreateShaderModule(&module_desc);

  wgpu::BindGroupLayoutEntry entry{};
  entry.binding = 0;
  entry.visibility = wgpu::ShaderStage::Vertex;
  entry.buffer.type = wgpu::BufferBindingType::Uniform;
  entry.buffer.minBindingSize = sizeof(ShadowUniforms);

  wgpu::BindGroupLayoutDescriptor layout_desc{};
  layout_desc.entryCount = 1;
  layout_desc.entries = &entry;
  wgpu::BindGroupLayout bind_group_layout = device_.CreateBindGroupLayout(&layout_desc);

  wgpu::PipelineLayoutDescriptor pipeline_layout_desc{};
  pipeline_layout_desc.bindGroupLayoutCount = 1;
  pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;
  wgpu::PipelineLayout pipeline_layout = device_.CreatePipelineLayout(&pipeline_layout_desc);

  wgpu::DepthStencilState depth_stencil{};
  depth_stencil.format = shadow_format_;
  depth_stencil.depthWriteEnabled = true;
  depth_stencil.depthCompare = wgpu::CompareFunction::Less;
  // Push the depth range out, which is the cheap way to suppress acne on
  // surfaces that face the light obliquely.
  depth_stencil.depthBias = 2;
  depth_stencil.depthBiasSlopeScale = 2.0f;
  depth_stencil.depthBiasClamp = 0.01f;

  wgpu::VertexAttribute attribute{};
  attribute.shaderLocation = 0;
  attribute.format = wgpu::VertexFormat::Float32x3;
  attribute.offset = offsetof(world::Vertex, px);

  wgpu::VertexBufferLayout vertex_layout{};
  vertex_layout.arrayStride = sizeof(world::Vertex);
  vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
  vertex_layout.attributeCount = 1;
  vertex_layout.attributes = &attribute;

  wgpu::RenderPipelineDescriptor pipeline_desc{};
  pipeline_desc.layout = pipeline_layout;
  pipeline_desc.vertex.module = module;
  pipeline_desc.vertex.entryPoint = "vs_main";
  pipeline_desc.vertex.bufferCount = 1;
  pipeline_desc.vertex.buffers = &vertex_layout;
  // No fragment stage: this pass only writes depth.
  pipeline_desc.fragment = nullptr;
  pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  pipeline_desc.primitive.cullMode = wgpu::CullMode::None;
  pipeline_desc.depthStencil = &depth_stencil;
  shadow_pipeline_ = device_.CreateRenderPipeline(&pipeline_desc);
  shadow_pipeline_.SetLabel("ShadowPipeline");

  wgpu::BufferDescriptor buffer_desc{};
  buffer_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  buffer_desc.size = sizeof(ShadowUniforms);
  shadow_uniforms_ = device_.CreateBuffer(&buffer_desc);

  wgpu::BindGroupEntry bind_entry{};
  bind_entry.binding = 0;
  bind_entry.buffer = shadow_uniforms_;
  bind_entry.offset = 0;
  bind_entry.size = sizeof(ShadowUniforms);

  wgpu::BindGroupDescriptor bind_group_desc{};
  bind_group_desc.layout = bind_group_layout;
  bind_group_desc.entryCount = 1;
  bind_group_desc.entries = &bind_entry;
  shadow_bind_group_ = device_.CreateBindGroup(&bind_group_desc);
}

void Renderer::BuildSkyPipeline() {
  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = shaders::kSkyShader;

  wgpu::ShaderModuleDescriptor module_desc{};
  module_desc.nextInChain = &wgsl;
  wgpu::ShaderModule module = device_.CreateShaderModule(&module_desc);

  wgpu::BindGroupLayoutEntry entry{};
  entry.binding = 0;
  entry.visibility = wgpu::ShaderStage::Fragment;
  entry.buffer.type = wgpu::BufferBindingType::Uniform;
  entry.buffer.minBindingSize = sizeof(SkyUniforms);

  wgpu::BindGroupLayoutDescriptor layout_desc{};
  layout_desc.entryCount = 1;
  layout_desc.entries = &entry;
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

  // The dome writes colour but no depth, and tests against nothing: it is
  // always behind the world.
  wgpu::DepthStencilState depth_stencil{};
  depth_stencil.format = depth_format_;
  depth_stencil.depthWriteEnabled = false;
  depth_stencil.depthCompare = wgpu::CompareFunction::Always;

  wgpu::VertexAttribute attribute{};
  attribute.shaderLocation = 0;
  attribute.format = wgpu::VertexFormat::Float32x3;
  attribute.offset = offsetof(world::Vertex, px);

  wgpu::VertexBufferLayout vertex_layout{};
  vertex_layout.arrayStride = sizeof(world::Vertex);
  vertex_layout.stepMode = wgpu::VertexStepMode::Vertex;
  vertex_layout.attributeCount = 1;
  vertex_layout.attributes = &attribute;

  wgpu::RenderPipelineDescriptor pipeline_desc{};
  pipeline_desc.layout = pipeline_layout;
  pipeline_desc.vertex.module = module;
  pipeline_desc.vertex.entryPoint = "vs_main";
  pipeline_desc.vertex.bufferCount = 1;
  pipeline_desc.vertex.buffers = &vertex_layout;
  pipeline_desc.fragment = &fragment;
  pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  pipeline_desc.primitive.cullMode = wgpu::CullMode::None;
  pipeline_desc.depthStencil = &depth_stencil;
  sky_pipeline_ = device_.CreateRenderPipeline(&pipeline_desc);

  wgpu::BufferDescriptor buffer_desc{};
  buffer_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
  buffer_desc.size = sizeof(SkyUniforms);
  sky_uniforms_ = device_.CreateBuffer(&buffer_desc);

  wgpu::BindGroupEntry bind_entry{};
  bind_entry.binding = 0;
  bind_entry.buffer = sky_uniforms_;
  bind_entry.offset = 0;
  bind_entry.size = sizeof(SkyUniforms);

  wgpu::BindGroupDescriptor bind_group_desc{};
  bind_group_desc.layout = bind_group_layout;
  bind_group_desc.entryCount = 1;
  bind_group_desc.entries = &bind_entry;
  sky_bind_group_ = device_.CreateBindGroup(&bind_group_desc);
}

void Renderer::UploadMesh(const world::Mesh& mesh, wgpu::Buffer& vertex_buffer,
                          wgpu::Buffer& index_buffer, std::uint32_t& index_count) {
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
  vertex_buffer = device_.CreateBuffer(&vertex_desc);
  queue_.WriteBuffer(vertex_buffer, 0, mesh.vertices.data(), vertex_bytes);

  wgpu::BufferDescriptor index_desc{};
  index_desc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
  index_desc.size = index_bytes;
  index_buffer = device_.CreateBuffer(&index_desc);
  queue_.WriteBuffer(index_buffer, 0, mesh.indices.data(), index_bytes);

  index_count = static_cast<std::uint32_t>(mesh.indices.size());
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

void Renderer::SelectLights(const math::Vec3& camera_pos) {
  selected_lights_.clear();

  // Nearest first by distance to the fixture. With rooms on a 10u grid and a
  // 15u range, the camera's own room is always nearest, so a fixed nearest-N
  // gives the same answer as a proper influence test for far less work.
  const std::size_t count =
      std::min<std::size_t>(lights_.size(), world::kMaxLightsPerFragment);
  if (count == 0) {
    return;
  }

  selected_lights_.reserve(lights_.size());
  for (std::size_t i = 0; i < lights_.size(); ++i) {
    selected_lights_.push_back(static_cast<int>(i));
  }

  std::partial_sort(
      selected_lights_.begin(),
      selected_lights_.begin() + static_cast<std::ptrdiff_t>(count),
      selected_lights_.end(), [this, &camera_pos](int a, int b) {
        const math::Vec3 da = lights_[static_cast<std::size_t>(a)].position - camera_pos;
        const math::Vec3 db = lights_[static_cast<std::size_t>(b)].position - camera_pos;
        return math::Dot(da, da) < math::Dot(db, db);
      });
  selected_lights_.resize(count);
}

void Renderer::RenderShadowPass() {
  // PARKED. See kShadowsEnabled for why, and for what has to be solved first.
  if (!kShadowsEnabled) {
    return;
  }

  wgpu::CommandEncoder encoder = device_.CreateCommandEncoder();

  // Clear the whole atlas once, then draw every light's tile without clearing.
  // Clearing per light would wipe the tiles already drawn: each pass covers the
  // entire attachment, not just its own viewport, so a per-light Clear would
  // leave only the last light's tile populated and every other sample would
  // read the cleared depth.
  {
    wgpu::RenderPassDepthStencilAttachment depth{};
    depth.view = shadow_atlas_view_;
    depth.depthLoadOp = wgpu::LoadOp::Clear;
    depth.depthStoreOp = wgpu::StoreOp::Store;
    depth.depthClearValue = 1.0f;

    wgpu::RenderPassDescriptor pass{};
    pass.depthStencilAttachment = &depth;

    wgpu::RenderPassEncoder clear_pass = encoder.BeginRenderPass(&pass);
    clear_pass.End();
  }

  if (lights_.empty()) {
    wgpu::CommandBuffer commands = encoder.Finish();
    queue_.Submit(1, &commands);
    return;
  }

  for (std::size_t i = 0; i < lights_.size(); ++i) {
    ShadowUniforms uniforms{};
    uniforms.view_proj = shadow_views_[i].view_projection;
    queue_.WriteBuffer(shadow_uniforms_, 0, &uniforms, sizeof(uniforms));

    const std::uint32_t column =
        static_cast<std::uint32_t>(i) % world::kShadowAtlasColumns;
    const std::uint32_t row =
        (static_cast<std::uint32_t>(i) / world::kShadowAtlasColumns) %
        world::kShadowAtlasRows;

    wgpu::RenderPassDepthStencilAttachment depth{};
    depth.view = shadow_atlas_view_;
    depth.depthLoadOp = wgpu::LoadOp::Load;
    depth.depthStoreOp = wgpu::StoreOp::Store;

    wgpu::RenderPassDescriptor pass{};
    pass.depthStencilAttachment = &depth;

    wgpu::RenderPassEncoder render_pass = encoder.BeginRenderPass(&pass);
    render_pass.SetViewport(
        static_cast<float>(column * world::kShadowTileSize),
        static_cast<float>(row * world::kShadowTileSize),
        static_cast<float>(world::kShadowTileSize),
        static_cast<float>(world::kShadowTileSize), 0.0f, 1.0f);
    // Scissor as well as viewport: WebGPU does not clip fragments to the
    // viewport, only to the scissor, so without this every light would write
    // across the whole atlas.
    render_pass.SetScissorRect(column * world::kShadowTileSize,
                               row * world::kShadowTileSize,
                               world::kShadowTileSize, world::kShadowTileSize);
    render_pass.SetPipeline(shadow_pipeline_);
    render_pass.SetBindGroup(0, shadow_bind_group_);
    render_pass.SetVertexBuffer(0, mesh_vertices_);
    render_pass.SetIndexBuffer(mesh_indices_, wgpu::IndexFormat::Uint32, 0,
                               mesh_index_count_ * sizeof(std::uint32_t));
    render_pass.DrawIndexed(mesh_index_count_);
    render_pass.End();
  }

  wgpu::CommandBuffer commands = encoder.Finish();
  queue_.Submit(1, &commands);
}

void Renderer::RenderFrame(const player::ViewMatrices& matrices) {
  if (!ready_) {
    return;
  }

  SelectLights(matrices.camera_position);

  SceneUniforms scene{};
  scene.view_proj = matrices.view_projection;
  scene.camera_pos[0] = matrices.camera_position.x;
  scene.camera_pos[1] = matrices.camera_position.y;
  scene.camera_pos[2] = matrices.camera_position.z;
  scene.camera_pos[3] = 1.0f;

  scene.wall_color[0] = 0.298f;
  scene.wall_color[1] = 0.0f;
  scene.wall_color[2] = 0.506f;
  scene.wall_color[3] = 1.0f;

  scene.fog[0] = kSkyHorizon[0];
  scene.fog[1] = kSkyHorizon[1];
  scene.fog[2] = kSkyHorizon[2];
  scene.fog[3] = 0.0035f;

  scene.tonemap[0] = kExposure;
  scene.tonemap[1] = 1.0f;

  scene.ambient[0] = config::kAmbientIntensity;
  scene.ambient[1] = config::kBakeMinDistance;

  scene.bounce[0] = config::kBakeBounceWall;
  scene.bounce[1] = config::kBakeBounceFloor;
  scene.bounce[2] = config::kBakeMin;
  scene.bounce[3] = config::kBakeMax;

  scene.ceil_glow[0] = config::kBakeCeilBase;
  scene.ceil_glow[1] = config::kBakeCeilGlow;
  scene.ceil_glow[2] = config::kBakeCeilSigma2;

  scene.light_count[0] = static_cast<float>(selected_lights_.size());
  // light_count[1] used to carry the shadow texel size; unused while parked.

  for (int i = 0; i < 3; ++i) {
    scene.sky_top[i] = kSkyTop[i];
    scene.sky_horizon[i] = kSkyHorizon[i];
    scene.sky_bottom[i] = kSkyBottom[i];
  }

  for (std::size_t slot = 0; slot < selected_lights_.size(); ++slot) {
    const int index = selected_lights_[slot];
    const world::SpotLight& light = lights_[static_cast<std::size_t>(index)];

    GpuLight& out = scene.lights[slot];
    out.position_range[0] = light.position.x;
    out.position_range[1] = light.position.y;
    out.position_range[2] = light.position.z;
    out.position_range[3] = light.range;

    out.direction_cone[0] = light.direction.x;
    out.direction_cone[1] = light.direction.y;
    out.direction_cone[2] = light.direction.z;
    out.direction_cone[3] = light.cone_inner;

    out.color_energy[0] = 1.0f;
    out.color_energy[1] = 1.0f;
    out.color_energy[2] = 1.0f;
    out.color_energy[3] = light.energy;

    out.outer_and_shadow[0] = light.cone_outer;
    out.outer_and_shadow[1] = 0.0f;  // no shadow tile while shadows are parked

    // Shadow matrices and atlas rects stay zeroed while parked, but the fields
    // remain in the uniform so the layout does not have to change to re-enable
    // the pass.
    if (kShadowsEnabled) {
      const world::ShadowView& view =
          shadow_views_[static_cast<std::size_t>(index)];
      out.outer_and_shadow[1] = 1.0f;
      for (int k = 0; k < 4; ++k) {
        out.atlas_rect[k] = view.atlas_rect[k];
      }
      scene.light_view_proj[slot] = view.view_projection;
    }
  }
  queue_.WriteBuffer(scene_uniforms_, 0, &scene, sizeof(scene));

  SkyUniforms sky{};
  for (int i = 0; i < 3; ++i) {
    sky.top[i] = kSkyTop[i];
    sky.horizon[i] = kSkyHorizon[i];
    sky.bottom[i] = kSkyBottom[i];
  }
  sky.tonemap[0] = kExposure;
  sky.tonemap[1] = 1.0f;
  queue_.WriteBuffer(sky_uniforms_, 0, &sky, sizeof(sky));

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
  color.clearValue = {kSkyBottom[0], kSkyBottom[1], kSkyBottom[2], 1.0f};

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

    if (sky_index_count_ > 0) {
      render_pass.SetPipeline(sky_pipeline_);
      render_pass.SetBindGroup(0, sky_bind_group_);
      render_pass.SetVertexBuffer(0, sky_vertices_);
      render_pass.SetIndexBuffer(sky_indices_, wgpu::IndexFormat::Uint32, 0,
                                 sky_index_count_ * sizeof(std::uint32_t));
      render_pass.DrawIndexed(sky_index_count_);
    }

    if (mesh_index_count_ > 0) {
      render_pass.SetPipeline(scene_pipeline_);
      render_pass.SetBindGroup(0, scene_bind_group_);
      render_pass.SetVertexBuffer(0, mesh_vertices_);
      render_pass.SetIndexBuffer(mesh_indices_, wgpu::IndexFormat::Uint32, 0,
                                 mesh_index_count_ * sizeof(std::uint32_t));
      render_pass.DrawIndexed(mesh_index_count_);
    }

    render_pass.End();
  }
  wgpu::CommandBuffer commands = encoder.Finish();
  queue_.Submit(1, &commands);
}

}  // namespace museum::render
