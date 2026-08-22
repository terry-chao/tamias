#ifndef TAMIAS_MESH_HLSLI
#define TAMIAS_MESH_HLSLI

struct PushConstants {
  float4x4 mvp;
  float4x4 model;
  float4 color;
  float4 material;           // x = roughness, y = metallic, z = has_albedo, w = has_normal
  float4 light_dir_selected; // xyz = light dir, w = selected flag
  float4 eye_pos_mode;       // xyz = eye world; w = render mode (mesh) or view distance (grid)
};

#if defined(TAMIAS_VULKAN)
[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;
#else
ConstantBuffer<PushConstants> pc : register(b0);
#endif

// Albedo 贴图 + 采样器。has_albedo 为 0 时仍须绑定合法资源（运行时用默认白纹理兜底）。
#if defined(TAMIAS_VULKAN)
[[vk::binding(0, 0)]] Texture2D albedo_tex;
[[vk::binding(1, 0)]] SamplerState albedo_samp;
#else
Texture2D albedo_tex : register(t0);
SamplerState albedo_samp : register(s0);
#endif

struct VsInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(1)]] float3 normal : NORMAL;
  [[vk::location(2)]] float2 uv : TEXCOORD0;
  [[vk::location(3)]] float3 color : COLOR;
};

struct VsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float3 normal : NORMAL;
  [[vk::location(1)]] float2 uv : TEXCOORD0;
  [[vk::location(2)]] float selected : TEXCOORD1;
  [[vk::location(3)]] float3 world_pos : TEXCOORD2;
  [[vk::location(4)]] float mode : TEXCOORD3;
  [[vk::location(5)]] float3 color : COLOR;
};

#endif // TAMIAS_MESH_HLSLI
