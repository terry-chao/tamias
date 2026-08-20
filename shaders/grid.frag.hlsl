#include "mesh.hlsli"

// 深色 CAD 地面网格：暗填充上叠浅色线，主次网格 + 世界轴 + 距离淡出。
float4 main(VsOutput input) : SV_Target0 {
  float2 coord = input.world_pos.xz;
  float2 deriv = fwidth(coord);

  // 次网格：每 1 个世界单位（与 engine/math/grid.h kGridMinorSpacing 一致）。
  float2 minor_d = min(frac(coord), 1.0 - frac(coord));
  float2 minor_px = minor_d / max(deriv, 1e-5);
  float minor_strength = 1.0 - saturate(min(minor_px.x, minor_px.y));

  // 主网格：每 5 个世界单位（与 kGridMajorSpacing 一致）。
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

  float3 fill = float3(0.22, 0.24, 0.28);         // 与天空地平线同色
  float3 minor_color = float3(0.38, 0.41, 0.46);  // 暗底上的浅次线
  float3 major_color = float3(0.50, 0.54, 0.60);

  float3 color = fill;
  color = lerp(color, minor_color, saturate(minor_strength * 0.50));
  color = lerp(color, major_color, saturate(major_strength * 0.75));

  // 世界轴：X 红、Z 蓝，比主网格略亮，便于定向。
  float axis_x = 1.0 - saturate(abs(coord.y) / max(deriv.y, 1e-5));
  float axis_z = 1.0 - saturate(abs(coord.x) / max(deriv.x, 1e-5));
  color = lerp(color, float3(0.78, 0.28, 0.28), saturate(axis_x));
  color = lerp(color, float3(0.28, 0.52, 0.88), saturate(axis_z));

  color = lerp(fill, color, fade);
  return float4(color, 1.0);
}
