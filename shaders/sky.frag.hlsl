// 深色工作室天空：顶部冷蓝灰 → 底部与网格填充同色，避免亮地平线抢构件。
struct VsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 main(VsOutput input) : SV_Target0 {
  const float3 sky_top = float3(0.14, 0.18, 0.24);
  const float3 horizon = float3(0.22, 0.24, 0.28);

  float t = saturate(input.uv.y);  // OpenGL：y=1 在顶部
#if defined(TAMIAS_VULKAN)
  t = 1.0 - t;                     // Vulkan：y=0 在顶部（NDC Y 向下）
#endif
  return float4(lerp(horizon, sky_top, t), 1.0);
}
