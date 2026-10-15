#include "mesh.hlsli"

struct LineVsOutput {
  float4 position : SV_Position;
  // x = 沿线参数 t，y = 到中线的**像素**距离（带符号），z = 半宽（像素）
  [[vk::location(0)]] float3 edge : TEXCOORD0;
  [[vk::location(1)]] float4 color : COLOR;
};

// 屏幕空间像素线的覆盖率：
//   到中线的距离在 [半宽 - 0.5, 半宽 + 0.5] 之间时按 1 px 的坡度淡出，
//   比这更近是实心、更远是 0——这就是抗锯齿，也顺带让「线宽 1 px」的线不至于
//   因为落不到像素中心就被吃成断续的点。
float4 main(LineVsOutput input) : SV_Target0 {
  const float coverage = saturate(input.edge.z + 0.5 - abs(input.edge.y));
  const float alpha = saturate(input.color.a) * coverage;
  // 预乘混合（ONE / ONE_MINUS_SRC_ALPHA，和文字 / 底图同一条通路）：rgb 先乘 alpha。
  return float4(input.color.rgb * alpha, alpha);
}
