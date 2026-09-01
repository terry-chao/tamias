#include "mesh.hlsli"

#if defined(TAMIAS_VULKAN)
[[vk::binding(0, 3)]] TextureCube prefilter_tex;
[[vk::binding(1, 3)]] SamplerState prefilter_samp;
#else
TextureCube prefilter_tex : register(t3);
SamplerState prefilter_samp : register(s3);
#endif

struct SkyVsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 main(SkyVsOutput input) : SV_Target0 {
  float2 ndc = input.uv * 2.0 - 1.0;
#if defined(TAMIAS_VULKAN)
  ndc.y = -ndc.y;
#endif
  float3 view_dir = normalize(float3(ndc.x * pc.color.x, ndc.y * pc.color.y, -1.0));
  float3 world_dir = mul((float3x3)pc.model, view_dir);
  float3 env = prefilter_tex.SampleLevel(prefilter_samp, world_dir, 1.2).rgb;
  env *= pc.lighting.x;
  env = env / (env + float3(0.85, 0.85, 0.85));
  return float4(env, 1.0);
}
