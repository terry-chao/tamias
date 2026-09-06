#pragma once

#include <string>
#include <string_view>

namespace tamias::webgpu_shaders {

inline constexpr std::string_view kPushAndMeshBindings = R"WGSL(
struct PushConstants {
  mvp: mat4x4<f32>,
  model: mat4x4<f32>,
  color: vec4<f32>,
  material: vec4<f32>,
  light_dir_selected: vec4<f32>,
  eye_pos_mode: vec4<f32>,
  lighting: vec4<f32>,
};
@group(0) @binding(0) var<uniform> pc: PushConstants;
@group(0) @binding(1) var albedo_tex: texture_2d<f32>;
@group(0) @binding(2) var tex_samp: sampler;
@group(0) @binding(3) var normal_tex: texture_2d<f32>;
@group(0) @binding(4) var irradiance_tex: texture_cube<f32>;
@group(0) @binding(5) var cube_samp: sampler;
@group(0) @binding(6) var prefilter_tex: texture_cube<f32>;
@group(0) @binding(7) var brdf_lut: texture_2d<f32>;
)WGSL";

inline std::string_view mesh_vert() {
  static const std::string src = std::string(kPushAndMeshBindings) + R"WGSL(
struct VsIn {
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec3<f32>,
};
struct VsOut {
  @builtin(position) position: vec4<f32>,
  @location(0) normal: vec3<f32>,
  @location(1) uv: vec2<f32>,
  @location(2) selected: f32,
  @location(3) world_pos: vec3<f32>,
  @location(4) mode: f32,
  @location(5) color: vec3<f32>,
};
@vertex
fn main(input: VsIn) -> VsOut {
  var o: VsOut;
  let world = pc.model * vec4<f32>(input.position, 1.0);
  o.world_pos = world.xyz;
  let model3 = mat3x3<f32>(pc.model[0].xyz, pc.model[1].xyz, pc.model[2].xyz);
  o.normal = model3 * input.normal;
  o.uv = input.uv;
  o.color = input.color;
  o.selected = pc.light_dir_selected.w;
  o.mode = pc.eye_pos_mode.w;
  o.position = pc.mvp * vec4<f32>(input.position, 1.0);
  return o;
}
)WGSL";
  return src;
}

inline std::string_view mesh_frag() {
  static const std::string src = std::string(kPushAndMeshBindings) + R"WGSL(
struct FsIn {
  @location(0) normal: vec3<f32>,
  @location(1) uv: vec2<f32>,
  @location(2) selected: f32,
  @location(3) world_pos: vec3<f32>,
  @location(4) mode: f32,
  @location(5) color: vec3<f32>,
};

fn shaded_simple(n: vec3<f32>, l: vec3<f32>, base: vec3<f32>) -> vec3<f32> {
  let ndotl = max(dot(n, l), 0.15);
  return base * ndotl;
}

fn sample_triplanar_albedo(world_pos: vec3<f32>, n: vec3<f32>) -> vec3<f32> {
  let wp = world_pos * 2.0;
  var blend = abs(n);
  blend = pow(blend, vec3<f32>(4.0));
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  let cx = textureSample(albedo_tex, tex_samp, wp.zy).rgb;
  let cy = textureSample(albedo_tex, tex_samp, wp.xz).rgb;
  let cz = textureSample(albedo_tex, tex_samp, wp.xy).rgb;
  return cx * blend.x + cy * blend.y + cz * blend.z;
}

fn unpack_normal(rgb: vec3<f32>) -> vec3<f32> { return rgb * 2.0 - 1.0; }

fn sample_triplanar_normal(world_pos: vec3<f32>, n: vec3<f32>) -> vec3<f32> {
  let wp = world_pos * 2.0;
  var blend = abs(n);
  blend = pow(blend, vec3<f32>(4.0));
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  let tx = unpack_normal(textureSample(normal_tex, tex_samp, wp.zy).xyz);
  let ty = unpack_normal(textureSample(normal_tex, tex_samp, wp.xz).xyz);
  let tz = unpack_normal(textureSample(normal_tex, tex_samp, wp.xy).xyz);
  let nx = vec3<f32>(tx.z, tx.y, tx.x);
  let ny = vec3<f32>(ty.x, ty.z, ty.y);
  let nz = vec3<f32>(tz.x, tz.y, tz.z);
  return normalize(n * vec3<f32>(blend.z + blend.y, blend.x + blend.z, blend.x + blend.y) +
                   nx * blend.x + ny * blend.y + nz * blend.z);
}

fn sample_uv_normal(n: vec3<f32>, world_pos: vec3<f32>, uv: vec2<f32>) -> vec3<f32> {
  let tnormal = unpack_normal(textureSample(normal_tex, tex_samp, uv).xyz);
  let dp1 = dpdx(world_pos);
  let dp2 = dpdy(world_pos);
  let duv1 = dpdx(uv);
  let duv2 = dpdy(uv);
  let dp2perp = cross(dp2, n);
  let dp1perp = cross(n, dp1);
  var t = dp2perp * duv1.x + dp1perp * duv2.x;
  var b = dp2perp * duv1.y + dp1perp * duv2.y;
  let inv = inverseSqrt(max(dot(t, t) * dot(b, b), 1e-8));
  t *= inv;
  b *= inv;
  return normalize(t * tnormal.x + b * tnormal.y + n * tnormal.z);
}

fn shaded_realistic(n: vec3<f32>, l: vec3<f32>, v: vec3<f32>, base: vec3<f32>, rough: f32,
                    metal: f32, opacity: f32) -> vec4<f32> {
  let PI = 3.14159265;
  let ndotl = max(dot(n, l), 0.0);
  let ndotv = max(dot(n, v), 1e-4);
  let h = normalize(l + v);
  let ndoth = max(dot(n, h), 1e-4);
  let vdoth = max(dot(v, h), 1e-4);
  let roughness = clamp(rough, 0.045, 1.0);
  let alpha = roughness * roughness;
  let alpha2 = alpha * alpha;
  let f0 = mix(vec3<f32>(0.04), base, metal);
  let F = f0 + (vec3<f32>(1.0) - f0) * pow(1.0 - vdoth, 5.0);
  let transmissive = select(0.0, 1.0, opacity < 0.999 && metal < 0.5);
  var kd = base * (1.0 - metal);
  if (transmissive > 0.5) {
    kd *= opacity;
  }
  let diffuse = (kd / PI) * ndotl * (vec3<f32>(1.0) - F);
  let denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
  let D = alpha2 / (PI * denom * denom);
  let k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  let gv = ndotv / (ndotv * (1.0 - k) + k);
  let gl = ndotl / (ndotl * (1.0 - k) + k);
  let G = gv * gl;
  let specular = (D * G * F) / max(4.0 * ndotv, 1e-4);
  let light_col = vec3<f32>(pc.lighting.y);
  let punctual_diff = diffuse * light_col;
  let punctual_spec = specular * light_col;
  let irradiance = textureSample(irradiance_tex, cube_samp, n).rgb;
  let R = reflect(-v, n);
  let prefiltered = textureSampleLevel(prefilter_tex, cube_samp, R, roughness * pc.lighting.z).rgb;
  let env_brdf = textureSample(brdf_lut, tex_samp, vec2<f32>(ndotv, roughness)).rg;
  let spec_ibl = prefiltered * (f0 * env_brdf.x + env_brdf.y);
  let diff_ibl = irradiance * kd;
  let spec = (punctual_spec + spec_ibl) * pc.lighting.x;
  let diff = (punctual_diff + diff_ibl) * pc.lighting.x;
  if (transmissive > 0.5) {
    let ldr_spec = spec / (spec + vec3<f32>(0.85));
    let ldr_diff = diff / (diff + vec3<f32>(0.85));
    let a = clamp(opacity + (1.0 - opacity) * pow(1.0 - ndotv, 5.0), 0.0, 1.0);
    return vec4<f32>(ldr_spec + ldr_diff * a, a);
  }
  var color = spec + diff;
  color = color / (color + vec3<f32>(0.85));
  return vec4<f32>(color, 1.0);
}

@fragment
fn main(input: FsIn) -> @location(0) vec4<f32> {
  if (input.mode > 2.5) {
    var c = input.color * pc.color.rgb;
    if (input.selected > 0.5) {
      c = vec3<f32>(0.35, 0.72, 1.0);
    }
    return vec4<f32>(c, 1.0);
  }
  if (input.mode < 0.5) {
    var wire = vec3<f32>(0.82, 0.86, 0.92);
    if (input.selected > 0.5) {
      wire = vec3<f32>(0.35, 0.72, 1.0);
    }
    return vec4<f32>(wire, 1.0);
  }
  var n = normalize(input.normal);
  let v = normalize(pc.eye_pos_mode.xyz - input.world_pos);
  if (dot(n, v) < 0.0) {
    discard;
  }
  if (pc.material.w > 0.5) {
    n = select(sample_triplanar_normal(input.world_pos, n),
               sample_uv_normal(n, input.world_pos, input.uv), pc.lighting.w > 0.5);
    n = normalize(n);
    if (dot(n, v) < 0.0) {
      n = -n;
    }
  }
  let l = normalize(pc.light_dir_selected.xyz);
  var base = pc.color.rgb * input.color;
  if (pc.material.z > 0.5) {
    base = select(sample_triplanar_albedo(input.world_pos, n),
                  textureSample(albedo_tex, tex_samp, input.uv).rgb, pc.lighting.w > 0.5);
  }
  var lit_rgb: vec3<f32>;
  var lit_a = 1.0;
  if (input.mode > 1.5) {
    let pbr = shaded_realistic(n, l, v, base, pc.material.x, pc.material.y, clamp(pc.color.w, 0.0, 1.0));
    lit_rgb = pbr.rgb;
    lit_a = pbr.a;
  } else {
    lit_rgb = shaded_simple(n, l, base);
  }
  if (input.selected > 0.5) {
    lit_rgb = mix(lit_rgb, vec3<f32>(0.28, 0.62, 1.0), 0.22);
    lit_rgb += vec3<f32>(0.03, 0.07, 0.14);
  }
  return vec4<f32>(lit_rgb, lit_a);
}
)WGSL";
  return src;
}

inline std::string_view sky_vert() {
  static const std::string src = R"WGSL(
struct VsIn {
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec3<f32>,
};
struct VsOut {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
};
@vertex
fn main(input: VsIn) -> VsOut {
  var o: VsOut;
  o.position = vec4<f32>(input.position, 1.0);
  o.uv = input.position.xy * 0.5 + 0.5;
  return o;
}
)WGSL";
  return src;
}

inline std::string_view sky_frag() {
  static const std::string src = std::string(kPushAndMeshBindings) + R"WGSL(
@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
  var ndc = uv * 2.0 - 1.0;
  ndc.y = -ndc.y;
  let view_dir = normalize(vec3<f32>(ndc.x * pc.color.x, ndc.y * pc.color.y, -1.0));
  let model3 = mat3x3<f32>(pc.model[0].xyz, pc.model[1].xyz, pc.model[2].xyz);
  let world_dir = model3 * view_dir;
  var env = textureSampleLevel(prefilter_tex, cube_samp, world_dir, 1.2).rgb;
  env *= pc.lighting.x;
  env = env / (env + vec3<f32>(0.85));
  return vec4<f32>(env, 1.0);
}
)WGSL";
  return src;
}

inline std::string_view grid_vert() {
  static const std::string src = std::string(kPushAndMeshBindings) + R"WGSL(
struct VsIn {
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec3<f32>,
};
struct VsOut {
  @builtin(position) position: vec4<f32>,
  @location(0) world_pos: vec3<f32>,
};
@vertex
fn main(input: VsIn) -> VsOut {
  var o: VsOut;
  let world = pc.model * vec4<f32>(input.position, 1.0);
  o.world_pos = world.xyz;
  o.position = pc.mvp * vec4<f32>(input.position, 1.0);
  return o;
}
)WGSL";
  return src;
}

inline std::string_view grid_frag() {
  static const std::string src = std::string(kPushAndMeshBindings) + R"WGSL(
fn grid_line(coord: vec2<f32>, spacing: f32, deriv: vec2<f32>) -> f32 {
  let cell = min(fract(coord / spacing), 1.0 - fract(coord / spacing));
  let px = cell * spacing / max(deriv, vec2<f32>(1e-5));
  return 1.0 - clamp(min(px.x, px.y), 0.0, 1.0);
}
@fragment
fn main(@location(0) world_pos: vec3<f32>) -> @location(0) vec4<f32> {
  let coord = world_pos.xz;
  let deriv = fwidth(coord);
  let world_per_pixel = max(max(deriv.x, deriv.y), 1e-5);
  let lod = log(max(world_per_pixel * 8.0, 1.0)) / log(10.0);
  let lod_base = floor(lod);
  let lod_frac = clamp(lod - lod_base, 0.0, 1.0);
  let minor_lo = pow(10.0, lod_base);
  let minor_hi = minor_lo * 10.0;
  let minor_strength = mix(grid_line(coord, minor_lo, deriv),
                           grid_line(coord, minor_hi, deriv), lod_frac);
  let major_strength = mix(grid_line(coord, minor_lo * 5.0, deriv),
                           grid_line(coord, minor_hi * 5.0, deriv), lod_frac);
  let view_scale = max(pc.eye_pos_mode.w, 1.0);
  let dist = length(world_pos.xz - pc.eye_pos_mode.xz);
  var fade = 1.0 - smoothstep(8.0 * view_scale, 32.0 * view_scale, dist);
  fade *= smoothstep(0.0, 0.4 * view_scale, dist);
  let fill = vec3<f32>(0.22, 0.24, 0.28);
  var color = fill;
  color = mix(color, vec3<f32>(0.38, 0.41, 0.46), clamp(minor_strength * 0.50, 0.0, 1.0));
  color = mix(color, vec3<f32>(0.50, 0.54, 0.60), clamp(major_strength * 0.75, 0.0, 1.0));
  let axis_x = 1.0 - clamp(abs(coord.y) / max(deriv.y, 1e-5), 0.0, 1.0);
  let axis_z = 1.0 - clamp(abs(coord.x) / max(deriv.x, 1e-5), 0.0, 1.0);
  color = mix(color, vec3<f32>(0.78, 0.28, 0.28), clamp(axis_x, 0.0, 1.0));
  color = mix(color, vec3<f32>(0.28, 0.52, 0.88), clamp(axis_z, 0.0, 1.0));
  color = mix(fill, color, fade);
  return vec4<f32>(color, 1.0);
}
)WGSL";
  return src;
}

}  // namespace tamias::webgpu_shaders
