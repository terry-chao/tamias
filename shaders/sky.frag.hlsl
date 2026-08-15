// 天空渐变：屏幕顶部天空蓝 → 底部地平线亮色，给出「天 / 地」的视觉界限。
struct VsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 main(VsOutput input) : SV_Target0 {
  const float3 sky_top = float3(0.24, 0.40, 0.60);
  const float3 horizon = float3(0.86, 0.88, 0.92);

  float t = saturate(input.uv.y);  // OpenGL：y=1 在顶部
#if defined(TAMIAS_VULKAN)
  t = 1.0 - t;                     // Vulkan：y=0 在顶部（NDC Y 向下）
#endif
  return float4(lerp(horizon, sky_top, t), 1.0);
}
