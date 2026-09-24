#include "render/texture.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdio>

namespace museum::render {
namespace {

constexpr std::uint32_t MipLevelCount(std::uint32_t width, std::uint32_t height) {
  std::uint32_t levels = 1;
  std::uint32_t size = width > height ? width : height;
  while (size > 1) {
    size >>= 1;
    ++levels;
  }
  return levels;
}

}  // namespace

Image LoadImage(const std::string& path) {
  int width = 0;
  int height = 0;
  int channels = 0;

  // Force RGBA so every texture has the same layout regardless of source.
  stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
  if (data == nullptr) {
    std::printf("Texture: failed to load %s: %s\n", path.c_str(),
                stbi_failure_reason());
    return {};
  }

  Image image;
  image.width = width;
  image.height = height;
  image.pixels.assign(data, data + static_cast<std::size_t>(width) * height * 4);
  stbi_image_free(data);
  return image;
}

wgpu::Texture CreateTextureFromImage(wgpu::Device& device, wgpu::Queue& queue,
                                     const Image& image, bool srgb, bool repeat) {
  if (image.empty()) {
    return nullptr;
  }

  const std::uint32_t width = static_cast<std::uint32_t>(image.width);
  const std::uint32_t height = static_cast<std::uint32_t>(image.height);
  const std::uint32_t bytes_per_row = width * 4u;

  wgpu::TextureDescriptor desc{};
  desc.size = {width, height, 1};
  desc.format = srgb ? wgpu::TextureFormat::RGBA8UnormSrgb
                     : wgpu::TextureFormat::RGBA8Unorm;
  desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
               wgpu::TextureUsage::RenderAttachment;
  desc.mipLevelCount = MipLevelCount(width, height);
  desc.dimension = wgpu::TextureDimension::e2D;
  wgpu::Texture texture = device.CreateTexture(&desc);

  wgpu::TexelCopyTextureInfo destination{};
  destination.texture = texture;
  destination.mipLevel = 0;
  destination.origin = {0, 0, 0};
  destination.aspect = wgpu::TextureAspect::All;

  wgpu::TexelCopyBufferLayout layout{};
  layout.offset = 0;
  layout.bytesPerRow = bytes_per_row;
  layout.rowsPerImage = height;

  wgpu::Extent3D write_size{width, height, 1};
  queue.WriteTexture(&destination, image.pixels.data(), bytes_per_row * height,
                     &layout, &write_size);

  // Generate the mip chain by rendering each level into the next. On the web
  // Dawn's JS shim has no generateMipmap helper (it exists natively, but not via
  // emdawnwebgpu), and doing it by hand keeps one code path for both targets.
  if (desc.mipLevelCount > 1) {
    wgpu::ShaderModule blit = [&device]() {
      static const char kBlitShader[] = R"(
        struct VOut { @builtin(position) pos : vec4<f32>, @location(0) uv : vec2<f32> };
        @group(0) @binding(0) var src : texture_2d<f32>;
        @group(0) @binding(1) var samp : sampler;

        @vertex
        fn vs(@builtin(vertex_index) i : u32) -> VOut {
          var p = array<vec2<f32>, 4>(
            vec2<f32>(-1.0, -1.0), vec2<f32>(1.0, -1.0),
            vec2<f32>(-1.0,  1.0), vec2<f32>(1.0,  1.0));
          var out : VOut;
          out.pos = vec4<f32>(p[i], 0.0, 1.0);
          out.uv = vec2<f32>((p[i].x + 1.0) * 0.5, (1.0 - p[i].y) * 0.5);
          return out;
        }

        @fragment
        fn fs(in : VOut) -> @location(0) vec4<f32> {
          return textureSample(src, samp, in.uv);
        }
      )";
      wgpu::ShaderSourceWGSL wgsl{};
      wgsl.code = kBlitShader;
      wgpu::ShaderModuleDescriptor module_desc{};
      module_desc.nextInChain = &wgsl;
      return device.CreateShaderModule(&module_desc);
    }();

    wgpu::SamplerDescriptor linear_sampler_desc{};
    linear_sampler_desc.magFilter = wgpu::FilterMode::Linear;
    linear_sampler_desc.minFilter = wgpu::FilterMode::Linear;
    wgpu::Sampler linear_sampler = device.CreateSampler(&linear_sampler_desc);

    wgpu::BindGroupLayoutEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Fragment;
    entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Fragment;
    entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.entryCount = 2;
    bgl_desc.entries = entries;
    wgpu::BindGroupLayout bgl = device.CreateBindGroupLayout(&bgl_desc);

    wgpu::PipelineLayoutDescriptor pl_desc{};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &bgl;
    wgpu::PipelineLayout pl = device.CreatePipelineLayout(&pl_desc);

    wgpu::ColorTargetState target{};
    target.format = desc.format;

    wgpu::FragmentState fragment{};
    fragment.module = blit;
    fragment.entryPoint = "fs";
    fragment.targetCount = 1;
    fragment.targets = &target;

    wgpu::RenderPipelineDescriptor pipeline_desc{};
    pipeline_desc.layout = pl;
    pipeline_desc.vertex.module = blit;
    pipeline_desc.vertex.entryPoint = "vs";
    pipeline_desc.fragment = &fragment;
    pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip;
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipeline_desc);

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    for (std::uint32_t level = 1; level < desc.mipLevelCount; ++level) {
      wgpu::TextureViewDescriptor src_view_desc{};
      src_view_desc.baseMipLevel = level - 1;
      src_view_desc.mipLevelCount = 1;
      wgpu::TextureView src_view = texture.CreateView(&src_view_desc);

      wgpu::TextureViewDescriptor dst_view_desc{};
      dst_view_desc.baseMipLevel = level;
      dst_view_desc.mipLevelCount = 1;
      wgpu::TextureView dst_view = texture.CreateView(&dst_view_desc);

      wgpu::BindGroupEntry bind_entries[2] = {};
      bind_entries[0].binding = 0;
      bind_entries[0].textureView = src_view;
      bind_entries[1].binding = 1;
      bind_entries[1].sampler = linear_sampler;
      wgpu::BindGroupDescriptor bg_desc{};
      bg_desc.layout = bgl;
      bg_desc.entryCount = 2;
      bg_desc.entries = bind_entries;
      wgpu::BindGroup bind_group = device.CreateBindGroup(&bg_desc);

      wgpu::RenderPassColorAttachment color{};
      color.view = dst_view;
      color.loadOp = wgpu::LoadOp::Clear;
      color.storeOp = wgpu::StoreOp::Store;
      color.clearValue = {0, 0, 0, 0};

      wgpu::RenderPassDescriptor pass{};
      pass.colorAttachmentCount = 1;
      pass.colorAttachments = &color;

      wgpu::RenderPassEncoder render_pass = encoder.BeginRenderPass(&pass);
      render_pass.SetPipeline(pipeline);
      render_pass.SetBindGroup(0, bind_group);
      render_pass.Draw(4);
      render_pass.End();
    }
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
  }

  return texture;
}

}  // namespace museum::render
