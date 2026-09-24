// Shaders for the museum scene.
//
// The lighting is still a placeholder fill (Batch 4 replaces it), but the
// colour pipeline is final: textures are sampled as sRGB, everything is
// computed in linear space, then tone-mapped and encoded to sRGB on output.
// Getting this order right is the difference between the museum matching the
// JS build and looking washed out.
#pragma once

namespace museum::render::shaders {

// The world pass: museum geometry, textured by material.
inline constexpr char kSceneShader[] = R"(
struct Uniforms {
  view_proj : mat4x4<f32>,
  camera_pos : vec4<f32>,
  wall_color : vec4<f32>,
  light_dir : vec4<f32>,
  sky_top : vec4<f32>,
  sky_horizon : vec4<f32>,
  sky_bottom : vec4<f32>,
  fog : vec4<f32>,          // rgb = fog colour, a = density
  tonemap : vec4<f32>,      // x = exposure, y = enabled
};
@group(0) @binding(0) var<uniform> u : Uniforms;
@group(0) @binding(1) var floor_tex : texture_2d<f32>;
@group(0) @binding(2) var floor_sampler : sampler;

struct VertexIn {
  @location(0) position : vec3<f32>,
  @location(1) normal : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) is_floor : f32,
};

struct VertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) normal : vec3<f32>,
  @location(1) world_pos : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) is_floor : f32,
};

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
  var out : VertexOut;
  out.position = u.view_proj * vec4<f32>(in.position, 1.0);
  out.normal = in.normal;
  out.world_pos = in.position;
  out.uv = in.uv;
  out.is_floor = in.is_floor;
  return out;
}

// ACES approximation (Narkowicz), the same curve most real-time engines use.
fn tonemap_aces(x : vec3<f32>) -> vec3<f32> {
  let a = 2.51;
  let b = 0.03;
  let c = 2.43;
  let d = 0.59;
  let e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

// Linear -> sRGB, matching the piecewise IEC curve.
fn linear_to_srgb(c : vec3<f32>) -> vec3<f32> {
  let cutoff = c < vec3<f32>(0.0031308);
  let low = c * 12.92;
  let high = 1.055 * pow(max(c, vec3<f32>(0.0)), vec3<f32>(1.0 / 2.4)) - 0.055;
  return select(high, low, cutoff);
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let is_floor = in.is_floor > 0.5;

  // Sample unconditionally, then select: WGSL requires textureSample to be in
  // uniform control flow, and `is_floor` varies per pixel (it is a vertex
  // attribute), so an `if` around the sample is a compile error.
  let tex = textureSample(floor_tex, floor_sampler, in.uv).rgb;
  // The source brick texture is very dark, so it is boosted; the JS museum
  // multiplies it by 2.2 for the same reason.
  let floor_albedo = min(tex * 2.2, vec3<f32>(1.0));  let albedo = select(u.wall_color.rgb, floor_albedo, is_floor);

  let n = normalize(in.normal);
  let key = max(dot(n, normalize(u.light_dir.xyz)), 0.0);
  let ambient = 0.35 + 0.25 * max(n.y, 0.0);
  let lit = albedo * (ambient + key * 0.9);

  // Distance haze toward the horizon colour, so far rooms read as far.
  let view_dir = in.world_pos - u.camera_pos.xyz;
  let distance = length(view_dir);
  let fog_amount = 1.0 - exp(-distance * u.fog.a);
  var color = mix(lit, u.fog.rgb, clamp(fog_amount, 0.0, 1.0));

  color = color * u.tonemap.x;
  if (u.tonemap.y > 0.5) {
    color = tonemap_aces(color);
  }
  return vec4<f32>(linear_to_srgb(color), 1.0);
}
)";

// The sky dome: a vertical gradient, drawn on the inside of a large sphere.
// Colours are authored in sRGB and converted to linear here so the gradient is
// mixed in linear space, then run through the same tonemap and encode as the
// world so the two meet seamlessly at the horizon.
inline constexpr char kSkyShader[] = R"(
struct SkyUniforms {
  top : vec4<f32>,
  horizon : vec4<f32>,
  bottom : vec4<f32>,
  tonemap : vec4<f32>,   // x = exposure, y = enabled
};
@group(0) @binding(0) var<uniform> u : SkyUniforms;

struct VOut {
  @builtin(position) pos : vec4<f32>,
  @location(0) dir : vec3<f32>,
};

@vertex
fn vs_main(@location(0) position : vec3<f32>) -> VOut {
  var out : VOut;
  out.dir = normalize(position);
  // Push to the far plane so the dome never clips into the world.
  out.pos = vec4<f32>(position.xy, 0.999999, 1.0);
  return out;
}

fn srgb_to_linear(c : vec3<f32>) -> vec3<f32> {
  let cutoff = c < vec3<f32>(0.04045);
  let low = c / 12.92;
  let high = pow((c + 0.055) / 1.055, vec3<f32>(2.4));
  return select(high, low, cutoff);
}

fn tonemap_aces(x : vec3<f32>) -> vec3<f32> {
  let a = 2.51;
  let b = 0.03;
  let c = 2.43;
  let d = 0.59;
  let e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

fn linear_to_srgb(c : vec3<f32>) -> vec3<f32> {
  let cutoff = c < vec3<f32>(0.0031308);
  let low = c * 12.92;
  let high = 1.055 * pow(max(c, vec3<f32>(0.0)), vec3<f32>(1.0 / 2.4)) - 0.055;
  return select(high, low, cutoff);
}

fn smoothstep01(edge0 : f32, edge1 : f32, x : f32) -> f32 {
  let t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

@fragment
fn fs_main(in : VOut) -> @location(0) vec4<f32> {
  let top = srgb_to_linear(u.top.rgb);
  let horizon = srgb_to_linear(u.horizon.rgb);
  let bottom = srgb_to_linear(u.bottom.rgb);

  let h = in.dir.y;
  var color : vec3<f32>;
  if (h >= 0.0) {
    color = mix(horizon, top, smoothstep01(0.0, 0.55, h));
  } else {
    color = mix(horizon, bottom, smoothstep01(0.0, 0.35, -h));
  }

  color = color * u.tonemap.x;
  if (u.tonemap.y > 0.5) {
    color = tonemap_aces(color);
  }
  return vec4<f32>(linear_to_srgb(color), 1.0);
}
)";

}  // namespace museum::render::shaders
