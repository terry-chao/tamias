#include "mesh.hlsli"

// 无限地面网格：基于世界 XZ 的 1 像素宽网格线（fwidth 抗锯齿），随距离淡出到地平线色。
float4 main(VsOutput input) : SV_Target0 {
  float2 coord = input.world_pos.xz;
  float2 deriv = fwidth(coord);

  // 距最近整数线的距离（0 在线上，0.5 在格子中心）→ 像素。
  float2 d = min(frac(coord), 1.0 - frac(coord));
  float2 px = d / deriv;
  float line_dist = min(px.x, px.y);
  float grid_strength = 1.0 - saturate(line_dist);

  // 距相机水平距离淡出，远处与天空地平线融为一体。
  float dist = length(input.world_pos.xz - pc.eye_pos_mode.xz);
  float fade = 1.0 - smoothstep(20.0, 80.0, dist);

  float3 grid_color = float3(0.42, 0.45, 0.50);
  float3 horizon = float3(0.86, 0.88, 0.92);
  return float4(lerp(horizon, grid_color, grid_strength * fade), 1.0);
}
