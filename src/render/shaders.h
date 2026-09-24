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
// SHADOWS ARE PARKED. The shadow atlas, the light-space matrices and the
// comparison sampler were built and are kept (see world/shadows.h and the
// shadow pass in renderer.cpp) but the sampling is disabled: every fragment is
// treated as lit. Two things surfaced while debugging it that must be solved
// before it can be switched on, and both are recorded rather than forgotten:
//
//   1. The per-axis resolve in CollisionWorld is unrelated, but the same class
//      of mistake bit the shadow pass: clearing per light wiped the atlas,
//      because a render pass covers the whole attachment and not just its
//      viewport. Fixed (clear once, then Load), but the sampling path was never
//      proven end to end.
//   2. The nearest-N light selection (SelectLights) counts a neighbouring
//      room's fixture as valid whenever its shadow lookup fails, so a room
//      corner was lit by a fixture behind a wall. Without working shadows this
//      is not fixable by selection alone, which is the real reason shadows are
//      a prerequisite rather than a polish item.
inline constexpr char kSceneShader[] = R"(
struct Light {
  position_range : vec4<f32>,   // xyz = fixture, w = range
  direction_cone : vec4<f32>,   // xyz = direction, w = cos(inner)
  color_energy : vec4<f32>,     // rgb = colour, a = energy
  outer_and_shadow : vec4<f32>, // x = cos(outer), y = 1 when shadowed
  atlas_rect : vec4<f32>,       // shadow atlas UV rect (unused while parked)
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
  light_count : vec4<f32>,  // x = count
  light_view_proj : array<mat4x4<f32>, 4>,
  lights : array<Light, 4>,
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

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {
  let is_floor = in.is_floor > 0.5;

  // Sample unconditionally, then select: WGSL requires textureSample to be in
  // uniform control flow, and `is_floor` varies per pixel (it is a vertex
  // attribute), so an `if` around the sample is a compile error.
  let tex = textureSample(floor_tex, floor_sampler, in.uv).rgb;
  // The source brick texture is very dark, so it is boosted; the JS museum
  // multiplies it by 2.2 for the same reason.
  let floor_albedo = min(tex * 2.2, vec3<f32>(1.0));
  let albedo = select(u.wall_color.rgb, floor_albedo, is_floor);

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

    lit = lit + albedo * light.color_energy.rgb * attenuation * cone * n_dot_l *
                influence;
  }

  // The JS bake clamps the total, which is what keeps a doorway between two
  // lit rooms from blowing out.
  let total = clamp(lit, vec3<f32>(u.bounce.z), vec3<f32>(u.bounce.w));

  // Distance haze toward the horizon colour, so far rooms read as far.
  let fog_amount = 1.0 - exp(-distance * u.fog.a);
  var color = mix(total, u.fog.rgb, clamp(fog_amount, 0.0, 1.0));

  color = color * u.tonemap.x;
  if (u.tonemap.y > 0.5) {
    color = tonemap_aces(color);
  }
  return vec4<f32>(linear_to_srgb(color), 1.0);
}
)";

// Packs a light's position and range for the shadow depth pass. PARKED: not
// bound by any pipeline while shadows are off, kept so the pass can be
// re-enabled without rewriting it.
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
