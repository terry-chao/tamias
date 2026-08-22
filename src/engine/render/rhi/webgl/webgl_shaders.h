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
} pc;
)GLSL";

inline std::string_view mesh_vert() {
  static const std::string src = std::string("#version 300 es\n") + std::string(kPushBlock) + R"GLSL(
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;
out vec3 v_normal;
out vec2 v_uv;
out float v_selected;
out vec3 v_world_pos;
out float v_mode;
out vec3 v_color;
void main() {
  vec4 world = pc.model * vec4(a_position, 1.0);
  v_world_pos = world.xyz;
  v_normal = mat3(pc.model) * a_normal;
  v_uv = a_uv;
  v_color = a_color;
  v_selected = pc.light_dir_selected.w;
  v_mode = pc.eye_pos_mode.w;
  gl_Position = pc.mvp * vec4(a_position, 1.0);
}
)GLSL";
  return src;
}

inline std::string_view mesh_frag() {
  static const std::string src = std::string("#version 300 es\nprecision highp float;\n") +
                                 std::string(kPushBlock) + R"GLSL(
uniform sampler2D albedo_tex;
in vec3 v_normal;
in vec2 v_uv;
in float v_selected;
in vec3 v_world_pos;
in float v_mode;
in vec3 v_color;
out vec4 frag_color;

vec3 shaded_simple(vec3 n, vec3 l, vec3 base) {
  float ndotl = max(dot(n, l), 0.15);
  return base * ndotl;
}

vec3 shaded_realistic(vec3 n, vec3 l, vec3 v, vec3 base, float rough, float metal) {
  const float PI = 3.14159265;
  float ndotl = max(dot(n, l), 0.0);
  float ndotv = max(dot(n, v), 1e-4);
  vec3 h = normalize(l + v);
  float ndoth = max(dot(n, h), 1e-4);
  float roughness = clamp(rough, 0.045, 1.0);
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  vec3 f0 = mix(vec3(0.04), base, metal);
  vec3 F = f0 + (vec3(1.0) - f0) * pow(1.0 - ndotv, 5.0);
  vec3 kd = base * (1.0 - metal);
  vec3 diffuse = kd * ndotl * (vec3(1.0) - F);
  float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
  float D = alpha2 / (PI * denom * denom);
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float gv = ndotv / (ndotv * (1.0 - k) + k);
  float gl = ndotl / (ndotl * (1.0 - k) + k);
  float G = gv * gl;
  vec3 specular = (D * G * F) / max(4.0 * ndotv, 1e-4);
  float hemi = clamp(0.5 * n.y + 0.5, 0.0, 1.0);
  vec3 env = mix(vec3(0.10, 0.11, 0.12), vec3(0.22, 0.26, 0.32), hemi);
  vec3 ambient_diffuse = env * kd;
  vec3 env_spec_color = mix(vec3(0.04), base, metal);
  vec3 ambient_specular = env * env_spec_color * (0.05 + 0.6 * (1.0 - roughness) * (1.0 - roughness));
  vec3 color = ambient_diffuse + ambient_specular + diffuse + specular;
  return color / (color + vec3(0.85));
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
  vec3 l = normalize(pc.light_dir_selected.xyz);
  vec3 base = pc.color.rgb * v_color;
  if (pc.material.z > 0.5) {
    vec3 wp = v_world_pos * 2.0;
    vec3 blend = abs(n);
    blend = pow(blend, vec3(4.0));
    blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
    vec3 cx = texture(albedo_tex, wp.zy).rgb;
    vec3 cy = texture(albedo_tex, wp.xz).rgb;
    vec3 cz = texture(albedo_tex, wp.xy).rgb;
    base = cx * blend.x + cy * blend.y + cz * blend.z;
  }
  vec3 lit = v_mode > 1.5
                 ? shaded_realistic(n, l, v, base, pc.material.x, pc.material.y)
                 : shaded_simple(n, l, base);
  if (v_selected > 0.5) {
    lit = mix(lit, vec3(0.28, 0.62, 1.0), 0.22);
    lit += vec3(0.03, 0.07, 0.14);
  }
  frag_color = vec4(lit, 1.0);
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
  static const std::string src = R"GLSL(#version 300 es
precision highp float;
in vec2 v_uv;
out vec4 frag_color;
void main() {
  vec3 sky_top = vec3(0.14, 0.18, 0.24);
  vec3 horizon = vec3(0.22, 0.24, 0.28);
  float t = clamp(v_uv.y, 0.0, 1.0);
  frag_color = vec4(mix(horizon, sky_top, t), 1.0);
}
)GLSL";
  return src;
}

inline std::string_view grid_vert() {
  static const std::string src = std::string("#version 300 es\n") + std::string(kPushBlock) + R"GLSL(
layout(location = 0) in vec3 a_position;
out vec3 v_world_pos;
void main() {
  vec4 world = pc.model * vec4(a_position, 1.0);
  v_world_pos = world.xyz;
  gl_Position = pc.mvp * vec4(a_position, 1.0);
}
)GLSL";
  return src;
}

inline std::string_view grid_frag() {
  static const std::string src = std::string("#version 300 es\nprecision highp float;\n") +
                                 std::string(kPushBlock) + R"GLSL(
in vec3 v_world_pos;
out vec4 frag_color;
float grid_line(vec2 coord, float spacing, vec2 deriv) {
  vec2 cell = min(fract(coord / spacing), 1.0 - fract(coord / spacing));
  vec2 px = cell * spacing / max(deriv, vec2(1e-5));
  return 1.0 - clamp(min(px.x, px.y), 0.0, 1.0);
}
void main() {
  vec2 coord = v_world_pos.xz;
  vec2 deriv = fwidth(coord);
  float world_per_pixel = max(max(deriv.x, deriv.y), 1e-5);
  float lod = log(max(world_per_pixel * 8.0, 1.0)) / log(10.0);
  float lod_base = floor(lod);
  float lod_frac = clamp(lod - lod_base, 0.0, 1.0);
  float minor_lo = pow(10.0, lod_base);
  float minor_hi = minor_lo * 10.0;
  float minor_strength = mix(grid_line(coord, minor_lo, deriv),
                             grid_line(coord, minor_hi, deriv), lod_frac);
  float major_strength = mix(grid_line(coord, minor_lo * 5.0, deriv),
                             grid_line(coord, minor_hi * 5.0, deriv), lod_frac);
  float view_scale = max(pc.eye_pos_mode.w, 1.0);
  float dist = length(v_world_pos.xz - pc.eye_pos_mode.xz);
  float fade = 1.0 - smoothstep(8.0 * view_scale, 32.0 * view_scale, dist);
  fade *= smoothstep(0.0, 0.4 * view_scale, dist);
  vec3 fill = vec3(0.22, 0.24, 0.28);
  vec3 color = fill;
  color = mix(color, vec3(0.38, 0.41, 0.46), clamp(minor_strength * 0.50, 0.0, 1.0));
  color = mix(color, vec3(0.50, 0.54, 0.60), clamp(major_strength * 0.75, 0.0, 1.0));
  float axis_x = 1.0 - clamp(abs(coord.y) / max(deriv.y, 1e-5), 0.0, 1.0);
  float axis_z = 1.0 - clamp(abs(coord.x) / max(deriv.x, 1e-5), 0.0, 1.0);
  color = mix(color, vec3(0.78, 0.28, 0.28), clamp(axis_x, 0.0, 1.0));
  color = mix(color, vec3(0.28, 0.52, 0.88), clamp(axis_z, 0.0, 1.0));
  color = mix(fill, color, fade);
  frag_color = vec4(color, 1.0);
}
)GLSL";
  return src;
}

}  // namespace tamias::webgl_shaders
