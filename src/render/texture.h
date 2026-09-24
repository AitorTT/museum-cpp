// Texture loading and sampling configuration.
//
// Decoding goes through stb_image, one public-domain header, so the same code
// reads the JPEG paintings and the floor texture on both web and native. The
// alternative, SDL_image, is a port on the web and a package on the desktop,
// which is two different things to keep working.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <webgpu/webgpu_cpp.h>

namespace museum::render {

// Decoded 8-bit RGBA image.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;  // RGBA8, row-major, top-left origin

  bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

// Reads a PNG or JPEG from disk (or the Emscripten virtual filesystem).
// Returns an empty Image on failure, after reporting why.
Image LoadImage(const std::string& path);

// Uploads an image as a sampled texture with mipmaps.
//
// `srgb` marks colour textures (floor, paintings) so the GPU linearises them on
// sample; leave it false for data textures that are already linear.
wgpu::Texture CreateTextureFromImage(wgpu::Device& device, wgpu::Queue& queue,
                                     const Image& image, bool srgb,
                                     bool repeat);

}  // namespace museum::render
