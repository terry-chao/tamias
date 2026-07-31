#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(push_constant) uniform PushConstants {
  mat4 mvp;
  vec4 color;
  vec4 light_dir_selected; // xyz = light dir, w = selected flag
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out float vSelected;

void main() {
  vNormal = inNormal;
  vUV = inUV;
  vSelected = pc.light_dir_selected.w;
  gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
