#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in float vSelected;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in float vMode;

layout(push_constant) uniform PushConstants {
  mat4 mvp;
  mat4 model;
  vec4 color;
  vec4 light_dir_selected;
  vec4 eye_pos_mode;
} pc;

layout(location = 0) out vec4 outColor;

vec3 shaded_simple(vec3 n, vec3 l, vec3 base) {
  float ndotl = max(dot(n, l), 0.15);
  return base * ndotl;
}

vec3 shaded_realistic(vec3 n, vec3 l, vec3 v, vec3 base) {
  // Hemisphere ambient (Z-up)
  float hemi = clamp(0.5 * n.z + 0.5, 0.0, 1.0);
  vec3 ambient = mix(vec3(0.06, 0.07, 0.09), vec3(0.28, 0.30, 0.34), hemi) * base;

  float ndotl = max(dot(n, l), 0.0);
  vec3 diffuse = base * ndotl;

  vec3 h = normalize(l + v);
  float spec = pow(max(dot(n, h), 0.0), 48.0);
  float fresnel = pow(1.0 - max(dot(n, v), 0.0), 3.0);
  vec3 specular = vec3(0.95, 0.97, 1.0) * spec * (0.25 + 0.55 * fresnel);

  // Soft fill from opposite side so cavities stay readable.
  float fill = max(dot(n, normalize(vec3(-l.x, -l.y, 0.35))), 0.0) * 0.18;
  vec3 color = ambient + diffuse * 0.9 + specular + base * fill;

  // Mild Reinhard keep highlights from clipping in sRGB swapchain.
  return color / (color + vec3(0.85));
}

void main() {
  // Wireframe: flat bright edges, no lighting (mode == 0).
  if (vMode < 0.5) {
    vec3 wire = vec3(0.92, 0.94, 0.97);
    if (vSelected > 0.5) {
      wire = vec3(1.0, 0.82, 0.35);
    }
    outColor = vec4(wire, 1.0);
    return;
  }

  vec3 n = normalize(vNormal);
  vec3 l = normalize(pc.light_dir_selected.xyz);
  vec3 base = pc.color.rgb;

  vec3 lit;
  if (vMode > 1.5) {
    vec3 v = normalize(pc.eye_pos_mode.xyz - vWorldPos);
    lit = shaded_realistic(n, l, v, base);
  } else {
    lit = shaded_simple(n, l, base);
  }

  if (vSelected > 0.5) {
    lit = mix(lit, vec3(1.0, 0.75, 0.2), 0.45);
  }
  outColor = vec4(lit, 1.0);
}
