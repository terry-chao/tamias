#version 450 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(std140, binding = 0) uniform PushConstants {
  mat4 mvp;
  mat4 model;
  vec4 color;
  vec4 light_dir_selected; // xyz = light dir, w = selected flag
  vec4 eye_pos_mode;       // xyz = eye world, w = render mode
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out float vSelected;
layout(location = 3) out vec3 vWorldPos;
layout(location = 4) out float vMode;

void main() {
  vec4 world = pc.model * vec4(inPosition, 1.0);
  vWorldPos = world.xyz;
  vNormal = mat3(pc.model) * inNormal;
  vUV = inUV;
  vSelected = pc.light_dir_selected.w;
  vMode = pc.eye_pos_mode.w;
  gl_Position = pc.mvp * vec4(inPosition, 1.0);
}
