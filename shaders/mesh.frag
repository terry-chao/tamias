#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in float vSelected;

layout(push_constant) uniform PushConstants {
  mat4 mvp;
  vec4 color;
  vec4 light_dir_selected;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(vNormal);
  vec3 l = normalize(pc.light_dir_selected.xyz);
  float ndotl = max(dot(n, l), 0.15);
  vec3 base = pc.color.rgb * ndotl;
  if (vSelected > 0.5) {
    base = mix(base, vec3(1.0, 0.75, 0.2), 0.45);
  }
  outColor = vec4(base, 1.0);
}
