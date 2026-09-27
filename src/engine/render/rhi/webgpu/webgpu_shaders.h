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
@group(0) @binding(8) var orm_tex: texture_2d<f32>;
)WGSL";

inline std::string_view mesh_vert() {
  static const std::string src = std::string(kPushAndMeshBindings) + R"WGSL(
struct VsIn {
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec3<f32>,
  @location(4) inst_row0: vec4<f32>,
  @location(5) inst_row1: vec4<f32>,
  @location(6) inst_row2: vec4<f32>,
  @location(7) inst_color: vec4<f32>,
  @location(8) inst_material: vec4<f32>,
  @location(9) inst_tex_st: vec4<f32>,
};
struct VsOut {
  @builtin(position) position: vec4<f32>,
  @location(0) normal: vec3<f32>,
  @location(1) uv: vec2<f32>,
  @location(2) selected: f32,
  @location(3) world_pos: vec3<f32>,
  @location(4) mode: f32,
  @location(5) color: vec3<f32>,
  @location(6) rough_metal: vec2<f32>,
  @location(7) opacity: f32,
  @location(8) world_scale: f32,
};
@vertex
fn main(input: VsIn) -> VsOut {
  var o: VsOut;
  let hp = vec4<f32>(input.position, 1.0);
  let world = vec3<f32>(dot(input.inst_row0, hp), dot(input.inst_row1, hp),
                        dot(input.inst_row2, hp));
  o.world_pos = world;
  o.normal = vec3<f32>(dot(input.inst_row0.xyz, input.normal),
                       dot(input.inst_row1.xyz, input.normal),
                       dot(input.inst_row2.xyz, input.normal));
  o.uv = input.uv * input.inst_tex_st.xy + input.inst_tex_st.zw;
  o.color = input.inst_color.rgb * input.color;
  o.selected = input.inst_material.z;
  o.mode = pc.eye_pos_mode.w;
  o.rough_metal = input.inst_material.xy;
  o.opacity = input.inst_color.a;
  o.world_scale = input.inst_material.w;
  o.position = pc.mvp * vec4<f32>(world, 1.0);
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
  @location(6) rough_metal: vec2<f32>,
  @location(7) opacity: f32,
  @location(8) world_scale: f32,
};

fn shaded_simple(n: vec3<f32>, l: vec3<f32>, base: vec3<f32>) -> vec3<f32> {
  let ndotl = max(dot(n, l), 0.15);
  return base * ndotl;
}

fn sample_triplanar_albedo(world_pos: vec3<f32>, n: vec3<f32>, world_scale: f32) -> vec3<f32> {
  let wp = world_pos * world_scale;
  var blend = abs(n);
  blend = pow(blend, vec3<f32>(4.0));
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  let cx = textureSample(albedo_tex, tex_samp, wp.zy).rgb;
  let cy = textureSample(albedo_tex, tex_samp, wp.xz).rgb;
  let cz = textureSample(albedo_tex, tex_samp, wp.xy).rgb;
  return cx * blend.x + cy * blend.y + cz * blend.z;
}

fn unpack_normal(rgb: vec3<f32>) -> vec3<f32> { return rgb * 2.0 - 1.0; }

fn sample_triplanar_normal(world_pos: vec3<f32>, n: vec3<f32>, world_scale: f32) -> vec3<f32> {
  let wp = world_pos * world_scale;
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

fn sample_triplanar_orm(world_pos: vec3<f32>, n: vec3<f32>, world_scale: f32) -> vec3<f32> {
  let wp = world_pos * world_scale;
  var blend = abs(n);
  blend = pow(blend, vec3<f32>(4.0));
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  let cx = textureSample(orm_tex, tex_samp, wp.zy).rgb;
  let cy = textureSample(orm_tex, tex_samp, wp.xz).rgb;
  let cz = textureSample(orm_tex, tex_samp, wp.xy).rgb;
  return cx * blend.x + cy * blend.y + cz * blend.z;
}

fn shaded_realistic(n: vec3<f32>, l: vec3<f32>, v: vec3<f32>, base: vec3<f32>, rough: f32,
                    metal: f32, opacity: f32, ao: f32) -> vec4<f32> {
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
  var kd = base * (1.0 - metal) * ao;
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
  // 模式一律读 pc（uniform push constant），不要读 input.mode——那是插值 varying，
  // Tint 会判定它「可能非一致」。而 textureSample / dpdx 这类带隐式导数的调用
  // 必须处在一致控制流里，否则整个着色器编译失败（WebGPU 的硬性规则；
  // 桌面 SPIR-V 没有这条限制，所以这段只有 WebGPU 会炸）。
  if (pc.eye_pos_mode.w > 2.5) {
    var c = input.color * pc.color.rgb;
    if (input.selected > 0.5) {
      c = vec3<f32>(0.35, 0.72, 1.0);
    }
    return vec4<f32>(c, 1.0);
  }
  if (pc.eye_pos_mode.w < 0.5) {
    var wire = vec3<f32>(0.82, 0.86, 0.92);
    if (input.selected > 0.5) {
      wire = vec3<f32>(0.35, 0.72, 1.0);
    }
    return vec4<f32>(wire, 1.0);
  }
  var n = normalize(input.normal);
  let v = normalize(pc.eye_pos_mode.xyz - input.world_pos);
  // 背面剔除延后到函数末尾：条件 discard 挡在采样之前，同样会让后续被判定为
  // 非一致控制流。输出一样（背面还是被丢掉），只是多算了一点。
  let backface = dot(n, v) < 0.0;
  if (pc.material.w > 0.5) {
    n = select(sample_triplanar_normal(input.world_pos, n, input.world_scale),
               sample_uv_normal(n, input.world_pos, input.uv), pc.lighting.w > 0.5);
    n = normalize(n);
    if (dot(n, v) < 0.0) {
      n = -n;
    }
  }
  let l = normalize(pc.light_dir_selected.xyz);
  var base = pc.color.rgb * input.color;
  if (pc.material.z > 0.5) {
    base = select(sample_triplanar_albedo(input.world_pos, n, input.world_scale),
                  textureSample(albedo_tex, tex_samp, input.uv).rgb, pc.lighting.w > 0.5);
  }
  var rough = input.rough_metal.x;
  var metal = input.rough_metal.y;
  var ao = 1.0;
  if (pc.light_dir_selected.w > 0.5) {
    let orm = select(sample_triplanar_orm(input.world_pos, n, input.world_scale),
                     textureSample(orm_tex, tex_samp, input.uv).rgb, pc.lighting.w > 0.5);
    ao = orm.r;
    rough = orm.g;
    metal = orm.b;
  }
  var lit_rgb: vec3<f32>;
  var lit_a = 1.0;
  if (pc.eye_pos_mode.w > 1.5) {
    let pbr = shaded_realistic(n, l, v, base, rough, metal,
                               clamp(input.opacity, 0.0, 1.0), ao);
    lit_rgb = pbr.rgb;
    lit_a = pbr.a;
  } else {
    // premultiplied alpha（src = One / dst = OneMinusSrcAlpha）：rgb 先乘 alpha。
    lit_a = clamp(input.opacity, 0.0, 1.0);
    lit_rgb = shaded_simple(n, l, base) * lit_a;
  }
  if (input.selected > 0.5) {
    lit_rgb = mix(lit_rgb, vec3<f32>(0.28, 0.62, 1.0), 0.22);
    lit_rgb += vec3<f32>(0.03, 0.07, 0.14);
  }
  if (backface) {
    discard;
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
// 视口背景：天空 + 地面 + 工作平面网格。和桌面端 shaders/sky.frag.hlsl 同一套算法
// 与配色（那边有逐段注释），改动要三份一起改。调色板按 sRGB 写，最后转线性。
const kSkyZenith = vec3<f32>(0.125, 0.188, 0.290);
const kSkyHorizon = vec3<f32>(0.431, 0.525, 0.639);
const kHorizonGlow = vec3<f32>(0.663, 0.737, 0.824);
const kGroundNear = vec3<f32>(0.118, 0.149, 0.188);
const kGridMinor = vec3<f32>(0.290, 0.345, 0.439);
const kGridMajor = vec3<f32>(0.420, 0.498, 0.639);
const kAxisXColor = vec3<f32>(0.690, 0.376, 0.376);
const kAxisZColor = vec3<f32>(0.376, 0.533, 0.784);
const kGroundY = 0.0;
const kGridBaseSpacing = 1.0;
const kGridMajorEvery = 5.0;
const kGridMinPixels = 8.0;
const kLog10 = 0.30102999566;

fn grid_line(coord: vec2<f32>, spacing: f32, deriv: vec2<f32>) -> f32 {
  let cell = abs(fract(coord / spacing + vec2<f32>(0.5)) - vec2<f32>(0.5));
  let px = cell * spacing / max(deriv, vec2<f32>(1e-5));
  return 1.0 - clamp(min(px.x, px.y), 0.0, 1.0);
}

fn axis_line(distance_world: f32, deriv: f32) -> f32 {
  return 1.0 - clamp(abs(distance_world) / max(deriv, 1e-5), 0.0, 1.0);
}

fn srgb_to_linear(c: vec3<f32>) -> vec3<f32> {
  let lo = c / 12.92;
  let hi = pow((c + vec3<f32>(0.055)) / 1.055, vec3<f32>(2.4));
  return mix(lo, hi, step(vec3<f32>(0.04045), c));
}

@fragment
fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
  // NDC 的 Y 向上（见 webgpu_device.cpp 的说明），uv 已经是「上 = 1」，
  // 所以这里不需要再翻一次；翻了会让天空采样方向上下颠倒。
  var ndc = uv * 2.0 - 1.0;
  let model3 = mat3x3<f32>(pc.model[0].xyz, pc.model[1].xyz, pc.model[2].xyz);
  let cam_right = model3 * vec3<f32>(1.0, 0.0, 0.0);
  let cam_up = model3 * vec3<f32>(0.0, 1.0, 0.0);
  let cam_fwd = model3 * vec3<f32>(0.0, 0.0, -1.0);
  let eye = pc.eye_pos_mode.xyz;
  let ray_scale = pc.color.xy;

  var ray_dir: vec3<f32>;
  var ray_origin: vec3<f32>;
  if (pc.material.x > 0.5) {  // 正交：平面视图 / 立面视图
    ray_dir = cam_fwd;
    ray_origin = eye + cam_right * (ndc.x * ray_scale.x) + cam_up * (ndc.y * ray_scale.y);
  } else {  // 透视
    ray_dir = normalize(cam_fwd + cam_right * (ndc.x * ray_scale.x) +
                        cam_up * (ndc.y * ray_scale.y));
    ray_origin = eye;
  }

  let denom = min(ray_dir.y, -1e-6);
  let t_raw = (kGroundY - ray_origin.y) / denom;
  let hit = select(0.0, 1.0, ray_dir.y < -1e-6 && t_raw > 0.0);
  let ground_point = ray_origin + ray_dir * max(t_raw, 0.0);
  let coord = ground_point.xz;
  let deriv = vec2<f32>(fwidth(coord.x), fwidth(coord.y));

  let elevation = clamp(ray_dir.y, 0.0, 1.0);
  let sky = mix(kSkyHorizon, kSkyZenith, pow(elevation, 0.55));

  let view_scale = max(pc.eye_pos_mode.w, 1.0);
  let dist = length(ground_point - eye);
  let grid_fade = 1.0 - smoothstep(5.0 * view_scale, 28.0 * view_scale, dist);
  var haze = smoothstep(2.0 * view_scale, 26.0 * view_scale, dist);
  haze = haze * hit + (1.0 - hit) * 0.55;
  var ground = mix(kGroundNear, kSkyHorizon, haze * 0.78);

  let precision_step = max(abs(coord.x), abs(coord.y)) * 1e-6;
  let world_per_pixel = max(max(deriv.x, deriv.y), precision_step);
  let lod = max(log2(max(world_per_pixel * kGridMinPixels / kGridBaseSpacing, 1.0)) * kLog10,
                0.0);
  let lod_base = floor(lod);
  let lod_frac = clamp(lod - lod_base, 0.0, 1.0);
  let minor_lo = kGridBaseSpacing * pow(10.0, lod_base);
  let minor_hi = minor_lo * 10.0;
  let minor = mix(grid_line(coord, minor_lo, deriv), grid_line(coord, minor_hi, deriv),
                  lod_frac);
  let major = mix(grid_line(coord, minor_lo * kGridMajorEvery, deriv),
                  grid_line(coord, minor_hi * kGridMajorEvery, deriv), lod_frac);
  ground = mix(ground, kGridMinor, clamp(minor * 0.55, 0.0, 1.0) * grid_fade * hit);
  ground = mix(ground, kGridMajor, clamp(major * 0.80, 0.0, 1.0) * grid_fade * hit);

  let axis_x = axis_line(coord.y, deriv.y) * grid_fade * hit;
  let axis_z = axis_line(coord.x, deriv.x) * grid_fade * hit;
  ground = mix(ground, kAxisXColor, clamp(axis_x, 0.0, 1.0) * 0.70);
  ground = mix(ground, kAxisZColor, clamp(axis_z, 0.0, 1.0) * 0.70);

  var on_ground = 0.0;
  if (ray_dir.y < -1e-6) {
    on_ground = 1.0;
  } else if (ray_dir.y <= 1e-6 && ray_origin.y <= kGroundY) {
    on_ground = 1.0;
  }
  var color = mix(sky, ground, on_ground);
  let band = pow(1.0 - clamp(abs(ray_dir.y) / 0.05, 0.0, 1.0), 3.0);
  color = mix(color, kHorizonGlow, band * 0.30);
  return vec4<f32>(srgb_to_linear(color), 1.0);
}
)WGSL";
  return src;
}

}  // namespace tamias::webgpu_shaders
