// Shaders for the museum scene.
//
// The lighting is still a placeholder fill (Batch 4 replaces it), but the
// colour pipeline is final: textures are sampled as sRGB, everything is
// computed in linear space, then tone-mapped and encoded to sRGB on output.
// Getting this order right is the difference between the museum matching the
// JS build and looking washed out.
#pragma once

namespace museum::render::shaders {

// The world pass: museum geometry, lit by the ceiling spots.
//
// The light model is the JS museum's bake model (energy/d^2, cone falloff,
// bounce floor) evaluated per-pixel instead of per-vertex.
//
// Shadows come from an atlas: each ceiling spot renders the museum from its own
// point of view into a 256x256 tile of one 2048x2048 depth map, and a fragment
// is shadowed if it lies behind the surface that its own spot can see. The
// approach was proven first in a standalone scene (render/shadow_test.cpp), and
// the two mistakes that scene caught are worth repeating here because both are
// silent otherwise:
//
//   1. textureSampleLevel with a sampler_comparison does not compile -- and a
//      shader that fails to compile renders nothing at all, so the symptom was a
//      black screen rather than a black shadow. The comparison sampler only
//      supports textureSampleCompare; there is no raw depth readback through it.
//   2. The light look-at must use z_axis = -(light target - light eye), not the
//      light direction itself. Getting the sign wrong inverts the depth test,
//      which reads as fully shadowed everywhere.
inline constexpr char kSceneShader[] = R"(
struct Light {
  position_range : vec4<f32>,   // xyz = fixture, w = range
  direction_cone : vec4<f32>,   // xyz = direction, w = cos(inner)
  color_energy : vec4<f32>,     // rgb = colour, a = energy
  outer_and_shadow : vec4<f32>, // x = cos(outer), y = shadow slot + 1 (0 = none)
  atlas_rect : vec4<f32>,       // shadow atlas UV rect: xy origin, zw size
};

struct Uniforms {
  view_proj : mat4x4<f32>,
  camera_pos : vec4<f32>,
  wall_color : vec4<f32>,
  sky_top : vec4<f32>,
  sky_horizon : vec4<f32>,
  sky_bottom : vec4<f32>,
  fog : vec4<f32>,          // rgb = fog colour, a = density
  tonemap : vec4<f32>,      // x = exposure, y = enabled
  ambient : vec4<f32>,      // x = ambient, y = min distance (E/d^2 clamp)
  bounce : vec4<f32>,       // x = wall, y = floor, z = min, w = max
  ceil_glow : vec4<f32>,    // x = base, y = glow, z = sigma^2
  light_count : vec4<f32>,  // x = count, y = shadow tile size in texels
  light_view_proj : array<mat4x4<f32>, 4>,
  lights : array<Light, 4>,
};
@group(0) @binding(0) var<uniform> u : Uniforms;
@group(0) @binding(1) var floor_tex : texture_2d<f32>;
@group(0) @binding(2) var floor_sampler : sampler;
@group(0) @binding(3) var shadow_atlas : texture_depth_2d;
@group(0) @binding(4) var shadow_sampler : sampler_comparison;
@group(0) @binding(5) var painting_atlas : texture_2d<f32>;

// Material ids, matching museum.h. 0 wall/ceiling, 1 floor, 2 painting canvas,
// 3 painting frame.
const MAT_WALL : f32 = 0.0;
const MAT_FLOOR : f32 = 1.0;
const MAT_PAINTING : f32 = 2.0;
const MAT_FRAME : f32 = 3.0;

struct VertexIn {
  @location(0) position : vec3<f32>,
  @location(1) normal : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) material : f32,
};

struct VertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) normal : vec3<f32>,
  @location(1) world_pos : vec3<f32>,
  @location(2) uv : vec2<f32>,
  @location(3) material : f32,
};

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
  var out : VertexOut;
  out.position = u.view_proj * vec4<f32>(in.position, 1.0);
  out.normal = in.normal;
  out.world_pos = in.position;
  out.uv = in.uv;
  out.material = in.material;
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

fn smoothstep01(edge0 : f32, edge1 : f32, x : f32) -> f32 {
  let t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// The E/d^2 falloff clamp from the JS bake.
fn d_squared_clamped(world_pos : vec3<f32>, light_pos : vec3<f32>, min_dist : f32) -> f32 {
  let delta = light_pos - world_pos;
  let d2 = dot(delta, delta);
  let minimum = min_dist * min_dist;
  return max(d2, minimum);
}

// Fraction of light a fragment receives from one spot's atlas tile: 1 outside
// the tile's shadow, 0 inside it.
//
// Four comparison taps in a small cross keep the edge from stair-stepping at
// this tile resolution. The taps are taken unconditionally and combined with
// select, because textureSampleCompare is only legal in uniform control flow.
fn shadow_term(slot : i32, atlas_rect : vec4<f32>, world_pos : vec3<f32>) -> f32 {
  let light_clip = u.light_view_proj[slot] * vec4<f32>(world_pos, 1.0);
  let ndc = light_clip.xyz / light_clip.w;
  let local_uv = ndc.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5);

  // Map the light's own [-1,1] box onto its tile in the atlas.
  let atlas_uv = atlas_rect.xy + local_uv * atlas_rect.zw;

  let texel = atlas_rect.zw / u.light_count.y;

  // A slope-scaled bias. The museum's surfaces are mostly axis-aligned, so a
  // constant term plus a fixed extra is enough and avoids a costly derivative.
  let bias = 0.0016 + 0.004 * (1.0 - abs(normalize(world_pos - u.camera_pos.xyz).y));
  let tested = ndc.z - bias;

  // Every tap is taken unconditionally and the out-of-range case is folded in
  // with select: textureSampleCompare requires uniform control flow, and a
  // per-fragment early-out around it is a shader compile error.
  var shadow = 0.0;
  shadow = shadow + textureSampleCompare(shadow_atlas, shadow_sampler, atlas_uv, tested);
  shadow = shadow + textureSampleCompare(shadow_atlas, shadow_sampler, atlas_uv + vec2<f32>(texel.x, 0.0), tested);
  shadow = shadow + textureSampleCompare(shadow_atlas, shadow_sampler, atlas_uv + vec2<f32>(-texel.x, 0.0), tested);
  shadow = shadow + textureSampleCompare(shadow_atlas, shadow_sampler, atlas_uv + vec2<f32>(0.0, texel.y), tested);
  shadow = shadow + textureSampleCompare(shadow_atlas, shadow_sampler, atlas_uv + vec2<f32>(0.0, -texel.y), tested);
  shadow = shadow * 0.2;

  // Outside the light's box there is nothing to sample, so the fragment is lit.
  let inside = ndc.z >= 0.0 && ndc.z <= 1.0;
  return select(1.0, shadow, inside);
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let is_floor = abs(in.material - MAT_FLOOR) < 0.5;

  // Sample unconditionally, then select: WGSL requires textureSample to be in
  // uniform control flow, and the material varies per pixel (it is a vertex
  // attribute), so an `if` around the sample is a compile error.
  let tex = textureSample(floor_tex, floor_sampler, in.uv).rgb;
  // The source brick texture is very dark, so it is boosted; the JS museum
  // multiplies it by 2.2 for the same reason.
  let floor_albedo = min(tex * 2.2, vec3<f32>(1.0));
  let is_frame = abs(in.material - MAT_FRAME) < 0.5;
  // FRAME_COLOR_LINEAR from the JS config, and lit like any other surface.
  let frame_albedo = vec3<f32>(0.2, 0.2, 0.2);
  var albedo = select(u.wall_color.rgb, floor_albedo, is_floor);
  albedo = select(albedo, frame_albedo, is_frame);

  let n = normalize(in.normal);
  let to_frag = in.world_pos - u.camera_pos.xyz;
  let distance = length(to_frag);

  // Constant bounce term: the floor and walls re-radiate some light, which is
  // what lifts the shadowed sides of every fixture in the JS bake.
  let bounce = select(u.bounce.x, u.bounce.y, is_floor);
  var lit = albedo * bounce * u.ambient.x;

  // Ceiling glow: a downward spot never lights the ceiling it hangs from
  // (n_dot_l is zero), so the JS bake added a separate gaussian pool of
  // brightness under each fixture. Same term here, keyed off how close the
  // fragment is to the nearest light along the ceiling.
  let up_facing = select(0.0, 1.0, n.y < -0.5);
  var ceil_pool = u.ceil_glow.x;
  if (up_facing > 0.5) {
    var nearest2 = 1.0e9;
    for (var j = 0; j < i32(u.light_count.x); j = j + 1) {
      let lp = u.lights[j].position_range.xyz;
      let dx = in.world_pos.x - lp.x;
      let dz = in.world_pos.z - lp.z;
      nearest2 = min(nearest2, dx * dx + dz * dz);
    }
    ceil_pool = u.ceil_glow.x + u.ceil_glow.y * exp(-nearest2 / u.ceil_glow.z);
  }
  lit = select(lit, albedo * ceil_pool, up_facing > 0.5);

  // No early-continue in this loop: keeping every light evaluated means the
  // contributions stay a straight sum, which is the shape the shadow lookup
  // needs once it is switched back on.
  let count = i32(u.light_count.x);
  for (var i = 0; i < count; i = i + 1) {
    let light = u.lights[i];
    let light_pos = light.position_range.xyz;
    let to_light = light_pos - in.world_pos;
    let light_distance = length(to_light);

    // Cone: full brightness inside the inner angle, fading to nothing at the
    // outer one. Separable from the distance term, as in the bake.
    let light_dir = normalize(light.direction_cone.xyz);
    let cos_angle = dot(-normalize(to_light), light_dir);
    let cone = smoothstep01(light.outer_and_shadow.x, light.direction_cone.w, cos_angle);

    let n_dot_l = max(dot(n, normalize(to_light)), 0.0);

    let in_range = select(0.0, 1.0, light_distance <= light.position_range.w);
    let facing = select(0.0, 1.0, n_dot_l > 0.0);
    let influence = cone * in_range * facing;

    let d2 = d_squared_clamped(in.world_pos, light_pos, u.ambient.y);
    let attenuation = light.color_energy.a / d2;

    // Sample unconditionally, then mask: textureSampleCompare needs uniform
    // control flow, and the slot is a uniform value so the sample itself is
    // safe here even where the light does not reach.
    let slot = i32(light.outer_and_shadow.y) - 1;
    var visibility = 1.0;
    if (slot >= 0) {
      visibility = shadow_term(slot, light.atlas_rect, in.world_pos);
    }

    lit = lit + albedo * light.color_energy.rgb * attenuation * cone * n_dot_l *
                influence * visibility;
  }

  // The JS bake clamps the total, which is what keeps a doorway between two
  // lit rooms from blowing out.
  let total = clamp(lit, vec3<f32>(u.bounce.z), vec3<f32>(u.bounce.w));

  // Distance haze toward the horizon colour, so far rooms read as far.
  let fog_amount = 1.0 - exp(-distance * u.fog.a);
  var color = mix(total, u.fog.rgb, clamp(fog_amount, 0.0, 1.0));

  // A painting canvas is unshaded: the JS museum draws it with a basic material
  // so the artwork reads at its own brightness rather than being dimmed by
  // wherever its wall happens to sit relative to a fixture. The atlas is sRGB,
  // so the sample arrives linear, matching the rest of the pipeline.
  let is_painting = abs(in.material - MAT_PAINTING) < 0.5;
  let canvas = textureSample(painting_atlas, floor_sampler, in.uv).rgb;
  color = select(color, canvas, is_painting);

  color = color * u.tonemap.x;
  if (u.tonemap.y > 0.5) {
    color = tonemap_aces(color);
  }
  return vec4<f32>(linear_to_srgb(color), 1.0);
}
)";

// The shadow depth pass: transforms museum vertices by one light's view-
// projection. No fragment stage, so only depth is written.
inline constexpr char kShadowShader[] = R"(
struct ShadowUniforms {
  view_proj : mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> u : ShadowUniforms;

@vertex
fn vs_main(@location(0) position : vec3<f32>) -> @builtin(position) vec4<f32> {
  return u.view_proj * vec4<f32>(position, 1.0);
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
