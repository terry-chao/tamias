#pragma once

#include <string>
#include <string_view>

namespace tamias::webgl_shaders {

inline constexpr std::string_view kPushBlock = R"GLSL(
layout(std140) uniform PushConstants {
  mat4 mvp;
  mat4 model;
  vec4 color;
  vec4 material;
  vec4 light_dir_selected;
  vec4 eye_pos_mode;
  vec4 lighting;
} pc;
)GLSL";

inline std::string_view mesh_vert() {
  static const std::string src = std::string("#version 300 es\n") + std::string(kPushBlock) + R"GLSL(
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;
layout(location = 4) in vec4 a_inst_row0;
layout(location = 5) in vec4 a_inst_row1;
layout(location = 6) in vec4 a_inst_row2;
layout(location = 7) in vec4 a_inst_color;
layout(location = 8) in vec4 a_inst_material;
layout(location = 9) in vec4 a_inst_tex_st;
out vec3 v_normal;
out vec2 v_uv;
out float v_selected;
out vec3 v_world_pos;
out float v_mode;
out vec3 v_color;
out vec2 v_rough_metal;
out float v_opacity;
out float v_world_scale;
void main() {
  vec4 hp = vec4(a_position, 1.0);
  vec3 world = vec3(dot(a_inst_row0, hp), dot(a_inst_row1, hp), dot(a_inst_row2, hp));
  v_world_pos = world;
  v_normal = vec3(dot(a_inst_row0.xyz, a_normal), dot(a_inst_row1.xyz, a_normal),
                  dot(a_inst_row2.xyz, a_normal));
  v_uv = a_uv * a_inst_tex_st.xy + a_inst_tex_st.zw;
  v_color = a_inst_color.rgb * a_color;
  v_selected = a_inst_material.z;
  v_mode = pc.eye_pos_mode.w;
  v_rough_metal = a_inst_material.xy;
  v_opacity = a_inst_color.a;
  v_world_scale = a_inst_material.w;
  gl_Position = pc.mvp * vec4(world, 1.0);
}
)GLSL";
  return src;
}

inline std::string_view mesh_frag() {
  static const std::string src = std::string("#version 300 es\nprecision highp float;\n") +
                                 std::string(kPushBlock) + R"GLSL(
uniform sampler2D albedo_tex;
uniform sampler2D normal_tex;
uniform samplerCube irradiance_tex;
uniform samplerCube prefilter_tex;
uniform sampler2D brdf_lut;
uniform sampler2D orm_tex;
in vec3 v_normal;
in vec2 v_uv;
in float v_selected;
in vec3 v_world_pos;
in float v_mode;
in vec3 v_color;
in vec2 v_rough_metal;
in float v_opacity;
in float v_world_scale;
out vec4 frag_color;

vec3 shaded_simple(vec3 n, vec3 l, vec3 base) {
  float ndotl = max(dot(n, l), 0.15);
  return base * ndotl;
}

vec3 sample_triplanar_albedo(vec3 world_pos, vec3 n, float world_scale) {
  vec3 wp = world_pos * world_scale;
  vec3 blend = abs(n);
  blend = pow(blend, vec3(4.0));
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  vec3 cx = texture(albedo_tex, wp.zy).rgb;
  vec3 cy = texture(albedo_tex, wp.xz).rgb;
  vec3 cz = texture(albedo_tex, wp.xy).rgb;
  return cx * blend.x + cy * blend.y + cz * blend.z;
}

vec3 unpack_normal(vec3 rgb) { return rgb * 2.0 - 1.0; }

vec3 sample_triplanar_normal(vec3 world_pos, vec3 n, float world_scale) {
  vec3 wp = world_pos * world_scale;
  vec3 blend = abs(n);
  blend = pow(blend, vec3(4.0));
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  vec3 tx = unpack_normal(texture(normal_tex, wp.zy).xyz);
  vec3 ty = unpack_normal(texture(normal_tex, wp.xz).xyz);
  vec3 tz = unpack_normal(texture(normal_tex, wp.xy).xyz);
  vec3 nx = vec3(tx.z, tx.y, tx.x);
  vec3 ny = vec3(ty.x, ty.z, ty.y);
  vec3 nz = vec3(tz.x, tz.y, tz.z);
  return normalize(n * vec3(blend.z + blend.y, blend.x + blend.z, blend.x + blend.y) +
                   nx * blend.x + ny * blend.y + nz * blend.z);
}

vec3 sample_uv_normal(vec3 n, vec3 world_pos, vec2 uv) {
  vec3 tnormal = unpack_normal(texture(normal_tex, uv).xyz);
  vec3 dp1 = dFdx(world_pos);
  vec3 dp2 = dFdy(world_pos);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, n);
  vec3 dp1perp = cross(n, dp1);
  vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
  float inv = inversesqrt(max(dot(t, t) * dot(b, b), 1e-8));
  t *= inv;
  b *= inv;
  return normalize(t * tnormal.x + b * tnormal.y + n * tnormal.z);
}

vec3 sample_triplanar_orm(vec3 world_pos, vec3 n, float world_scale) {
  vec3 wp = world_pos * world_scale;
  vec3 blend = abs(n);
  blend = pow(blend, vec3(4.0));
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  vec3 cx = texture(orm_tex, wp.zy).rgb;
  vec3 cy = texture(orm_tex, wp.xz).rgb;
  vec3 cz = texture(orm_tex, wp.xy).rgb;
  return cx * blend.x + cy * blend.y + cz * blend.z;
}

vec4 shaded_realistic(vec3 n, vec3 l, vec3 v, vec3 base, float rough, float metal, float opacity,
                      float ao) {
  const float PI = 3.14159265;
  float ndotl = max(dot(n, l), 0.0);
  float ndotv = max(dot(n, v), 1e-4);
  vec3 h = normalize(l + v);
  float ndoth = max(dot(n, h), 1e-4);
  float vdoth = max(dot(v, h), 1e-4);
  float roughness = clamp(rough, 0.045, 1.0);
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  vec3 f0 = mix(vec3(0.04), base, metal);
  vec3 F = f0 + (vec3(1.0) - f0) * pow(1.0 - vdoth, 5.0);
  float transmissive = (opacity < 0.999 && metal < 0.5) ? 1.0 : 0.0;
  vec3 kd = base * (1.0 - metal) * ao;
  if (transmissive > 0.5) {
    kd *= opacity;
  }
  vec3 diffuse = (kd / PI) * ndotl * (vec3(1.0) - F);
  float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
  float D = alpha2 / (PI * denom * denom);
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float gv = ndotv / (ndotv * (1.0 - k) + k);
  float gl = ndotl / (ndotl * (1.0 - k) + k);
  float G = gv * gl;
  vec3 specular = (D * G * F) / max(4.0 * ndotv, 1e-4);
  vec3 light_col = vec3(pc.lighting.y);
  vec3 punctual_diff = diffuse * light_col;
  vec3 punctual_spec = specular * light_col;
  vec3 irradiance = texture(irradiance_tex, n).rgb;
  vec3 R = reflect(-v, n);
  vec3 prefiltered = textureLod(prefilter_tex, R, roughness * pc.lighting.z).rgb;
  vec2 env_brdf = texture(brdf_lut, vec2(ndotv, roughness)).rg;
  vec3 spec_ibl = prefiltered * (f0 * env_brdf.x + env_brdf.y);
  vec3 diff_ibl = irradiance * kd;
  vec3 spec = (punctual_spec + spec_ibl) * pc.lighting.x;
  vec3 diff = (punctual_diff + diff_ibl) * pc.lighting.x;
  if (transmissive > 0.5) {
    vec3 ldr_spec = spec / (spec + vec3(0.85));
    vec3 ldr_diff = diff / (diff + vec3(0.85));
    float a = clamp(opacity + (1.0 - opacity) * pow(1.0 - ndotv, 5.0), 0.0, 1.0);
    return vec4(ldr_spec + ldr_diff * a, a);
  }
  vec3 color = spec + diff;
  color = color / (color + vec3(0.85));
  return vec4(color, 1.0);
}

void main() {
  if (v_mode > 2.5) {
    vec3 c = v_color * pc.color.rgb;
    if (v_selected > 0.5) {
      c = vec3(0.35, 0.72, 1.0);
    }
    frag_color = vec4(c, 1.0);
    return;
  }
  if (v_mode < 0.5) {
    vec3 wire = vec3(0.82, 0.86, 0.92);
    if (v_selected > 0.5) {
      wire = vec3(0.35, 0.72, 1.0);
    }
    frag_color = vec4(wire, 1.0);
    return;
  }
  vec3 n = normalize(v_normal);
  vec3 v = normalize(pc.eye_pos_mode.xyz - v_world_pos);
  if (dot(n, v) < 0.0) {
    discard;
  }
  if (pc.material.w > 0.5) {
    n = pc.lighting.w > 0.5 ? sample_uv_normal(n, v_world_pos, v_uv)
                            : sample_triplanar_normal(v_world_pos, n, v_world_scale);
    n = normalize(n);
    if (dot(n, v) < 0.0) {
      n = -n;
    }
  }
  vec3 l = normalize(pc.light_dir_selected.xyz);
  vec3 base = pc.color.rgb * v_color;
  if (pc.material.z > 0.5) {
    base = pc.lighting.w > 0.5 ? texture(albedo_tex, v_uv).rgb
                               : sample_triplanar_albedo(v_world_pos, n, v_world_scale);
  }
  float rough = v_rough_metal.x;
  float metal = v_rough_metal.y;
  float ao = 1.0;
  if (pc.light_dir_selected.w > 0.5) {
    vec3 orm = pc.lighting.w > 0.5 ? texture(orm_tex, v_uv).rgb
                                   : sample_triplanar_orm(v_world_pos, n, v_world_scale);
    ao = orm.r;
    rough = orm.g;
    metal = orm.b;
  }
  vec3 lit_rgb;
  float lit_a = 1.0;
  if (v_mode > 1.5) {
    vec4 pbr = shaded_realistic(n, l, v, base, rough, metal, clamp(v_opacity, 0.0, 1.0), ao);
    lit_rgb = pbr.rgb;
    lit_a = pbr.a;
  } else {
    // premultiplied alpha（glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)）：
    // rgb 先乘 alpha。opacity == 1 时与不透明路径逐位一致。
    lit_a = clamp(v_opacity, 0.0, 1.0);
    lit_rgb = shaded_simple(n, l, base) * lit_a;
  }
  if (v_selected > 0.5) {
    lit_rgb = mix(lit_rgb, vec3(0.28, 0.62, 1.0), 0.22);
    lit_rgb += vec3(0.03, 0.07, 0.14);
  }
  frag_color = vec4(lit_rgb, lit_a);
}
)GLSL";
  return src;
}

// ---------------------------------------------------------------------------
// 屏幕空间文字（对应 shaders/text.vert.hlsl / text.frag.hlsl）
//
// 和 mesh 一样把 kPushBlock 逐字拼上：std140 的偏移必须与 C++ PushConstants 对齐。
// 单位四边形 (0..1)² 由实例里的**像素矩形**摆到屏幕，UV 从字形图集取。
// 实例复用 GpuInstance 布局（见 runtime/text_quad.h）：
//   row0.xy = 矩形左上角（像素，左上原点）   row0.zw = 矩形尺寸（像素）
//   color   = rgb + a（不预乘，片元里乘上去）
//   tex_st  = xy: 图集 UV 缩放，zw: 图集 UV 偏移
// 视口尺寸走 pc.eye_pos_mode.xy——文字不光照、不挑显示模式，那几个字段本来没人用。
// ---------------------------------------------------------------------------
inline std::string_view text_vert() {
  static const std::string src = std::string("#version 300 es\n") + std::string(kPushBlock) + R"GLSL(
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;
layout(location = 4) in vec4 a_rect;
layout(location = 7) in vec4 a_color;
layout(location = 9) in vec4 a_tex_st;
out vec2 v_uv;
out vec4 v_color;
void main() {
  vec2 viewport = max(pc.eye_pos_mode.xy, vec2(1.0));
  vec2 pixels = a_rect.xy + a_position.xy * a_rect.zw;
  float ndc_x = pixels.x / viewport.x * 2.0 - 1.0;
  // GL 裁剪空间 Y 向上，而像素 y 向下，要翻一下（Vulkan 分支见 text.vert.hlsl）。
  float ndc_y = 1.0 - pixels.y / viewport.y * 2.0;
  gl_Position = vec4(ndc_x, ndc_y, 0.0, 1.0);
  v_uv = a_tex_st.zw + a_uv * a_tex_st.xy;
  v_color = a_color;
}
)GLSL";
  return src;
}

inline std::string_view text_frag() {
  static const std::string src = std::string("#version 300 es\nprecision highp float;\n") +
                                 std::string(kPushBlock) + R"GLSL(
uniform sampler2D atlas_tex;
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
void main() {
  // 字形图集是「白 + 预乘 alpha」，覆盖度就在 alpha 里。
  // 输出同样预乘（rgb × alpha），和管线里的 ONE / ONE_MINUS_SRC_ALPHA 对齐。
  float coverage = texture(atlas_tex, v_uv).a;
  float alpha = clamp(coverage * v_color.a, 0.0, 1.0);
  frag_color = vec4(v_color.rgb * alpha, alpha);
}
)GLSL";
  return src;
}

// ---------------------------------------------------------------------------
// 图纸底图（对应 shaders/overlay.vert.hlsl / overlay.frag.hlsl）
// 单位四边形（局部 (u,v) ∈ [0,1]²，y = 0）由 pc.mvp 摆到世界，不参与光照。
// 管线不绑实例缓冲，所以只声明自己用得到的顶点属性。
// ---------------------------------------------------------------------------
inline std::string_view overlay_vert() {
  static const std::string src = std::string("#version 300 es\n") + std::string(kPushBlock) + R"GLSL(
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;
out vec2 v_uv;
out vec3 v_color;
out float v_opacity;
void main() {
  gl_Position = pc.mvp * vec4(a_position, 1.0);
  v_uv = a_uv;
  v_color = a_color;
  v_opacity = pc.color.a;
}
)GLSL";
  return src;
}

inline std::string_view overlay_frag() {
  static const std::string src = std::string("#version 300 es\nprecision highp float;\n") +
                                 std::string(kPushBlock) + R"GLSL(
uniform sampler2D albedo_tex;
in vec2 v_uv;
in vec3 v_color;
in float v_opacity;
out vec4 frag_color;
void main() {
  // 参考图纸底图：不做光照，直接把透明底的线稿贴到平面上。
  // 混合因子是 ONE / ONE_MINUS_SRC_ALPHA（预乘 alpha），所以 rgb 要先乘 alpha。
  vec4 tex = texture(albedo_tex, v_uv);
  float alpha = clamp(tex.a * v_opacity, 0.0, 1.0);
  frag_color = vec4(tex.rgb * v_color * alpha, alpha);
}
)GLSL";
  return src;
}

// ---------------------------------------------------------------------------
// 屏幕空间像素线（对应 shaders/line.vert.hlsl / line.frag.hlsl）
//
// 一个实例 = 一条**世界坐标线段** + 一个**像素**线宽。顶点把两个端点投影到屏幕，
// 在屏幕空间按法向把线段撑成四边形；片元再按「到中线的像素距离」算覆盖率。
// 实例复用 GpuInstance 布局（见 runtime/line_instance.h）：
//   row0 = xyz: 起点（世界）, w: 线宽（像素）
//   row1 = xyz: 终点（世界）, w: 未用
//   color = rgb + a（不预乘，片元里乘上去）
// 顶点属性里 position.xy 是单位四边形的角 (t, side)：t ∈ {0,1} 沿线参数，
// side ∈ {0,1} 横向（shader 里映射成 -1 / +1）。
// ---------------------------------------------------------------------------
inline std::string_view line_vert() {
  static const std::string src = std::string("#version 300 es\n") + std::string(kPushBlock) + R"GLSL(
layout(location = 0) in vec3 a_position;
layout(location = 4) in vec4 a_inst_row0;
layout(location = 5) in vec4 a_inst_row1;
layout(location = 7) in vec4 a_inst_color;
out vec3 v_edge;
out vec4 v_color;

// w 小于它就认为端点在相机背后（除下去会把点翻到屏幕另一头）。
const float kNearEps = 1e-4;
// 四边形横向额外撑出去这么多像素，留给片元算边缘覆盖率（抗锯齿那 1 px）。
const float kFeather = 1.0;

// 裁剪空间 → 像素（左上原点）。GL 的裁剪空间 Y 向上，和 text_vert 同一处约定。
vec2 pixel_from_ndc(vec2 ndc, vec2 viewport) {
  return vec2((ndc.x * 0.5 + 0.5) * viewport.x, (0.5 - ndc.y * 0.5) * viewport.y);
}

vec2 ndc_from_pixel(vec2 pixel, vec2 viewport) {
  return vec2(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0);
}

void main() {
  vec2 viewport = max(pc.eye_pos_mode.xy, vec2(1.0));
  float t = a_position.x;
  float side = a_position.y * 2.0 - 1.0;
  float half_width = max(a_inst_row0.w, 0.5) * 0.5;

  vec4 clip_a = pc.mvp * vec4(a_inst_row0.xyz, 1.0);
  vec4 clip_b = pc.mvp * vec4(a_inst_row1.xyz, 1.0);
  // 端点跑到相机背后：整段丢掉，或把背后那一端压到近平面上，让线看起来是被裁掉的。
  if (clip_a.w < kNearEps && clip_b.w < kNearEps) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);  // 视锥外，光栅化阶段整条丢掉
    v_edge = vec3(t, 0.0, half_width);
    v_color = a_inst_color;
    return;
  }
  if (clip_a.w < kNearEps) {
    clip_a = mix(clip_a, clip_b, (kNearEps - clip_a.w) / (clip_b.w - clip_a.w));
  } else if (clip_b.w < kNearEps) {
    clip_b = mix(clip_b, clip_a, (kNearEps - clip_b.w) / (clip_a.w - clip_b.w));
  }

  vec2 a_px = pixel_from_ndc(clip_a.xy / clip_a.w, viewport);
  vec2 b_px = pixel_from_ndc(clip_b.xy / clip_b.w, viewport);
  // 线段在屏幕上退化成一个点时给个默认方向，免得归一化出 NaN。
  vec2 delta = b_px - a_px;
  float len = length(delta);
  vec2 dir = len > 1e-4 ? delta / len : vec2(1.0, 0.0);
  vec2 normal = vec2(-dir.y, dir.x);

  float offset = side * (half_width + kFeather);
  vec2 px = mix(a_px, b_px, t) + normal * offset;

  // 深度按沿线参数线性插值；w 人为置 1，所以后面的变化量按屏幕空间线性插值就对了
  // （这条管线本来不测深度，值是给将来的 depth-tested 变体留的）。
  float z_over_w = mix(clip_a.z / clip_a.w, clip_b.z / clip_b.w, t);
  vec2 ndc = ndc_from_pixel(px, viewport);
  gl_Position = vec4(ndc.x, ndc.y, z_over_w, 1.0);
  // x = 沿线参数 t，y = 到中线的**像素**距离（带符号），z = 半宽（像素）
  v_edge = vec3(t, offset, half_width);
  v_color = a_inst_color;
}
)GLSL";
  return src;
}

inline std::string_view line_frag() {
  static const std::string src = R"GLSL(#version 300 es
precision highp float;
in vec3 v_edge;
in vec4 v_color;
out vec4 frag_color;
void main() {
  // 到中线的距离在 [半宽 - 0.5, 半宽 + 0.5] 之间时按 1 px 的坡度淡出，
  // 比这更近是实心、更远是 0——这就是抗锯齿。
  float coverage = clamp(v_edge.z + 0.5 - abs(v_edge.y), 0.0, 1.0);
  float alpha = clamp(v_color.a, 0.0, 1.0) * coverage;
  // 预乘混合（ONE / ONE_MINUS_SRC_ALPHA，和文字 / 底图同一条通路）：rgb 先乘 alpha。
  frag_color = vec4(v_color.rgb * alpha, alpha);
}
)GLSL";
  return src;
}

inline std::string_view sky_vert() {
  static const std::string src = R"GLSL(#version 300 es
layout(location = 0) in vec3 a_position;
out vec2 v_uv;
void main() {
  gl_Position = vec4(a_position, 1.0);
  v_uv = a_position.xy * 0.5 + 0.5;
}
)GLSL";
  return src;
}

inline std::string_view sky_frag() {
  static const std::string src = std::string("#version 300 es\nprecision highp float;\n") +
                                 std::string(kPushBlock) + R"GLSL(
in vec2 v_uv;
out vec4 frag_color;

// 视口背景：天空 + 地面 + 工作平面网格。和桌面端 shaders/sky.frag.hlsl 同一套算法
// 与配色（那边有逐段注释），改动要三份一起改。调色板按 sRGB 写，最后转线性。
const vec3 kSkyZenith = vec3(0.125, 0.188, 0.290);
const vec3 kSkyHorizon = vec3(0.431, 0.525, 0.639);
const vec3 kHorizonGlow = vec3(0.663, 0.737, 0.824);
const vec3 kGroundNear = vec3(0.118, 0.149, 0.188);
const vec3 kGridMinor = vec3(0.290, 0.345, 0.439);
const vec3 kGridMajor = vec3(0.420, 0.498, 0.639);
const vec3 kAxisXColor = vec3(0.690, 0.376, 0.376);
const vec3 kAxisZColor = vec3(0.376, 0.533, 0.784);
const float kGroundY = 0.0;
const float kGridBaseSpacing = 1.0;
const float kGridMajorEvery = 5.0;
const float kGridMinPixels = 8.0;
const float kLog10 = 0.30102999566;

float grid_line(vec2 coord, float spacing, vec2 deriv) {
  vec2 cell = abs(fract(coord / spacing + 0.5) - 0.5);
  vec2 px = cell * spacing / max(deriv, vec2(1e-5));
  return 1.0 - clamp(min(px.x, px.y), 0.0, 1.0);
}

float axis_line(float distance_world, float deriv) {
  return 1.0 - clamp(abs(distance_world) / max(deriv, 1e-5), 0.0, 1.0);
}

vec3 srgb_to_linear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main() {
  vec2 ndc = v_uv * 2.0 - 1.0;
  mat3 cam = mat3(pc.model);
  vec3 cam_right = cam * vec3(1.0, 0.0, 0.0);
  vec3 cam_up = cam * vec3(0.0, 1.0, 0.0);
  vec3 cam_fwd = cam * vec3(0.0, 0.0, -1.0);
  vec3 eye = pc.eye_pos_mode.xyz;
  vec2 ray_scale = pc.color.xy;

  vec3 ray_dir;
  vec3 ray_origin;
  if (pc.material.x > 0.5) {  // 正交：平面视图 / 立面视图
    ray_dir = cam_fwd;
    ray_origin = eye + cam_right * (ndc.x * ray_scale.x) + cam_up * (ndc.y * ray_scale.y);
  } else {  // 透视
    ray_dir = normalize(cam_fwd + cam_right * (ndc.x * ray_scale.x) +
                        cam_up * (ndc.y * ray_scale.y));
    ray_origin = eye;
  }

  float denom = min(ray_dir.y, -1e-6);
  float t_raw = (kGroundY - ray_origin.y) / denom;
  float hit = (ray_dir.y < -1e-6 && t_raw > 0.0) ? 1.0 : 0.0;
  vec3 ground_point = ray_origin + ray_dir * max(t_raw, 0.0);
  vec2 coord = ground_point.xz;
  vec2 deriv = vec2(fwidth(coord.x), fwidth(coord.y));

  float elevation = clamp(ray_dir.y, 0.0, 1.0);
  vec3 sky = mix(kSkyHorizon, kSkyZenith, pow(elevation, 0.55));

  float view_scale = max(pc.eye_pos_mode.w, 1.0);
  float dist = length(ground_point - eye);
  float grid_fade = 1.0 - smoothstep(5.0 * view_scale, 28.0 * view_scale, dist);
  float haze = smoothstep(2.0 * view_scale, 26.0 * view_scale, dist);
  haze = haze * hit + (1.0 - hit) * 0.55;
  vec3 ground = mix(kGroundNear, kSkyHorizon, haze * 0.78);

  float precision_step = max(abs(coord.x), abs(coord.y)) * 1e-6;
  float world_per_pixel = max(max(deriv.x, deriv.y), precision_step);
  float lod = max(log2(max(world_per_pixel * kGridMinPixels / kGridBaseSpacing, 1.0)) *
                      kLog10,
                  0.0);
  float lod_base = floor(lod);
  float lod_frac = clamp(lod - lod_base, 0.0, 1.0);
  float minor_lo = kGridBaseSpacing * pow(10.0, lod_base);
  float minor_hi = minor_lo * 10.0;
  float minor = mix(grid_line(coord, minor_lo, deriv), grid_line(coord, minor_hi, deriv),
                    lod_frac);
  float major = mix(grid_line(coord, minor_lo * kGridMajorEvery, deriv),
                    grid_line(coord, minor_hi * kGridMajorEvery, deriv), lod_frac);
  ground = mix(ground, kGridMinor, clamp(minor * 0.55, 0.0, 1.0) * grid_fade * hit);
  ground = mix(ground, kGridMajor, clamp(major * 0.80, 0.0, 1.0) * grid_fade * hit);

  float axis_x = axis_line(coord.y, deriv.y) * grid_fade * hit;
  float axis_z = axis_line(coord.x, deriv.x) * grid_fade * hit;
  ground = mix(ground, kAxisXColor, clamp(axis_x, 0.0, 1.0) * 0.70);
  ground = mix(ground, kAxisZColor, clamp(axis_z, 0.0, 1.0) * 0.70);

  float on_ground = 0.0;
  if (ray_dir.y < -1e-6) {
    on_ground = 1.0;
  } else if (ray_dir.y <= 1e-6 && ray_origin.y <= kGroundY) {
    on_ground = 1.0;
  }
  vec3 color = mix(sky, ground, on_ground);
  float band = pow(1.0 - clamp(abs(ray_dir.y) / 0.05, 0.0, 1.0), 3.0);
  color = mix(color, kHorizonGlow, band * 0.30);
  frag_color = vec4(srgb_to_linear(color), 1.0);
}
)GLSL";
  return src;
}

}  // namespace tamias::webgl_shaders
