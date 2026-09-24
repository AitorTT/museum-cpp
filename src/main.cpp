// Museum - C++/WebGPU bootstrap (Batch 0).
//
// One source builds for the web (Emscripten + emdawnwebgpu) and, from
// Batch 1 on, natively (Dawn or wgpu-native). This file draws a single
// triangle to prove the toolchain, the surface, and the frame loop work.
//
// Structure and API calls follow Emscripten's own
// test/webgpu_basic_rendering.cpp, as documented in carver/OptimizedC++Web.txt.

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <cstdio>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace {

constexpr uint32_t kInitialWidth = 640;
constexpr uint32_t kInitialHeight = 480;

const char kShader[] = R"(
@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4<f32> {
    var pos = array<vec2<f32>, 3>(
        vec2<f32>( 0.0,  0.6),
        vec2<f32>(-0.6, -0.6),
        vec2<f32>( 0.6, -0.6));
    return vec4<f32>(pos[idx], 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4<f32> {
    return vec4<f32>(1.0, 0.35, 0.1, 1.0);
}
)";

wgpu::Instance       gInstance;
wgpu::Adapter        gAdapter;
wgpu::Device         gDevice;
wgpu::Queue          gQueue;
wgpu::Surface        gSurface;
wgpu::RenderPipeline gPipeline;
wgpu::TextureFormat  gFormat = wgpu::TextureFormat::BGRA8Unorm;
uint32_t             gWidth  = kInitialWidth;
uint32_t             gHeight = kInitialHeight;

void LogMessage(wgpu::StringView message) {
  if (message.length) {
    std::printf("%.*s\n", static_cast<int>(message.length), message.data);
  }
}

void BuildPipeline() {
  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = kShader;

  wgpu::ShaderModuleDescriptor smDesc{};
  smDesc.nextInChain = &wgsl;
  wgpu::ShaderModule module = gDevice.CreateShaderModule(&smDesc);

  wgpu::ColorTargetState colorTarget{};
  colorTarget.format = gFormat;

  wgpu::FragmentState fragment{};
  fragment.module = module;
  fragment.targetCount = 1;
  fragment.targets = &colorTarget;

  wgpu::PipelineLayoutDescriptor plDesc{};
  plDesc.bindGroupLayoutCount = 0;
  plDesc.bindGroupLayouts = nullptr;

  wgpu::RenderPipelineDescriptor rpDesc{};
  rpDesc.layout = gDevice.CreatePipelineLayout(&plDesc);
  rpDesc.vertex.module = module;
  rpDesc.vertex.entryPoint = "vs_main";
  rpDesc.fragment = &fragment;
  rpDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  rpDesc.depthStencil = nullptr;

  gPipeline = gDevice.CreateRenderPipeline(&rpDesc);
}

void ConfigureSurface() {
#ifdef __EMSCRIPTEN__
  int canvasWidth = 0;
  int canvasHeight = 0;
  emscripten_get_canvas_element_size("#canvas", &canvasWidth, &canvasHeight);
  if (canvasWidth > 0 && canvasHeight > 0) {
    gWidth = static_cast<uint32_t>(canvasWidth);
    gHeight = static_cast<uint32_t>(canvasHeight);
  }
#endif

  wgpu::SurfaceCapabilities caps{};
  gSurface.GetCapabilities(gAdapter, &caps);
  if (caps.formatCount > 0) {
    gFormat = caps.formats[0];
  }

  wgpu::SurfaceConfiguration config{};
  config.device = gDevice;
  config.format = gFormat;
  config.usage = wgpu::TextureUsage::RenderAttachment;
  config.width = gWidth;
  config.height = gHeight;
  config.alphaMode = wgpu::CompositeAlphaMode::Auto;
  config.presentMode = wgpu::PresentMode::Fifo;
  gSurface.Configure(&config);
}

void RenderFrame() {
  wgpu::SurfaceTexture st{};
  gSurface.GetCurrentTexture(&st);
  if (st.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
      st.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
    return;
  }
  wgpu::TextureView view = st.texture.CreateView();

  wgpu::RenderPassColorAttachment color{};
  color.view = view;
  color.loadOp = wgpu::LoadOp::Clear;
  color.storeOp = wgpu::StoreOp::Store;
  color.clearValue = {0.05f, 0.05f, 0.08f, 1.0f};

  wgpu::RenderPassDescriptor pass{};
  pass.colorAttachmentCount = 1;
  pass.colorAttachments = &color;

  wgpu::CommandEncoder encoder = gDevice.CreateCommandEncoder();
  {
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    rp.SetPipeline(gPipeline);
    rp.Draw(3);
    rp.End();
  }
  wgpu::CommandBuffer cmd = encoder.Finish();
  gQueue.Submit(1, &cmd);
}

void Start() {
  gQueue = gDevice.GetQueue();
  ConfigureSurface();
  BuildPipeline();
  std::printf("Museum: device ready, surface %ux%u\n", gWidth, gHeight);

#ifdef __EMSCRIPTEN__
  // requestAnimationFrame loop; keeps the runtime alive.
  emscripten_set_main_loop(RenderFrame, 0, true);
#else
  // Native: drive this from the window's main loop (Batch 1).
#endif
}

void OnDeviceReady(wgpu::RequestDeviceStatus status,
                   wgpu::Device device,
                   wgpu::StringView message) {
  LogMessage(message);
  if (status != wgpu::RequestDeviceStatus::Success) {
    return;
  }
  gDevice = device;

#ifdef __EMSCRIPTEN__
  wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
  canvas.selector = "#canvas";
  wgpu::SurfaceDescriptor surfaceDesc{};
  surfaceDesc.nextInChain = &canvas;
  gSurface = gInstance.CreateSurface(&surfaceDesc);
#else
  // Native surface comes from the window handle (Batch 1).
#endif
  Start();
}

void OnAdapterReady(wgpu::RequestAdapterStatus status,
                    wgpu::Adapter adapter,
                    wgpu::StringView message) {
  LogMessage(message);
  if (status != wgpu::RequestAdapterStatus::Success) {
    return;
  }
  gAdapter = adapter;

  wgpu::DeviceDescriptor desc{};
  desc.SetUncapturedErrorCallback(
      [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView msg) {
        std::printf("UncapturedError (%d): ", static_cast<int>(type));
        LogMessage(msg);
      });
  adapter.RequestDevice(&desc, wgpu::CallbackMode::AllowSpontaneous, OnDeviceReady);
}

}  // namespace

int main() {
  gInstance = wgpu::CreateInstance();
  gInstance.RequestAdapter(nullptr, wgpu::CallbackMode::AllowSpontaneous, OnAdapterReady);
  return 0;
}
