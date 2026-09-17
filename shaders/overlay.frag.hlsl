#include "mesh.hlsli"

#if defined(TAMIAS_VULKAN)
[[vk::binding(0, 0)]] Texture2D albedo_tex;
[[vk::binding(1, 0)]] SamplerState albedo_samp;
#else
Texture2D albedo_tex : register(t0);
SamplerState albedo_samp : register(s0);
#endif

struct OverlayVsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float3 color : COLOR;
  [[vk::location(2)]] float opacity : TEXCOORD1;
};

// 参考图纸底图：不做光照，直接把透明底的线稿贴到平面上。
// 混合因子是 ONE / ONE_MINUS_SRC_ALPHA（预乘 alpha），所以 rgb 要先乘 alpha。
float4 main(OverlayVsOutput input) : SV_Target0 {
  float4 tex = albedo_tex.Sample(albedo_samp, input.uv);
  float alpha = saturate(tex.a * input.opacity);
  return float4(tex.rgb * input.color * alpha, alpha);
}
