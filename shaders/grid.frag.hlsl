#include "mesh.hlsli"

// 无限地面网格：主次网格 + 世界轴 + fwidth 抗锯齿，随距离淡出到地平线色。
float4 main(VsOutput input) : SV_Target0 {
  float2 coord = input.world_pos.xz;
  float2 deriv = fwidth(coord);

  // 次网格：每 1 个世界单位。
  float2 minor_d = min(frac(coord), 1.0 - frac(coord));
  float2 minor_px = minor_d / max(deriv, 1e-5);
  float minor_strength = 1.0 - saturate(min(minor_px.x, minor_px.y));

  // 主网格：每 5 个世界单位。
  const float major_scale = 5.0;
  float2 major_d = min(frac(coord / major_scale), 1.0 - frac(coord / major_scale));
  float2 major_px = major_d * major_scale / max(deriv, 1e-5);
  float major_strength = 1.0 - saturate(min(major_px.x, major_px.y));

  // 距相机水平距离淡出，远处与天空地平线融为一体。
  float dist = length(input.world_pos.xz - pc.eye_pos_mode.xz);
  float fade = 1.0 - smoothstep(40.0, 160.0, dist);
  // 相机脚下避免一条线贴在镜头前。
  float near_fade = smoothstep(0.0, 2.0, dist);
  fade *= near_fade;

  float3 minor_color = float3(0.54, 0.56, 0.60);
  float3 major_color = float3(0.34, 0.36, 0.42);
  float3 horizon = float3(0.86, 0.88, 0.92);

  float3 color = horizon;
  color = lerp(color, minor_color, saturate(minor_strength * 0.55));
  color = lerp(color, major_color, saturate(major_strength * 0.85));
  color = lerp(horizon, color, fade);
  return float4(color, 1.0);
}
