#include "mesh.hlsli"

float grid_line(float2 coord, float spacing, float2 deriv) {
  float2 cell = min(frac(coord / spacing), 1.0 - frac(coord / spacing));
  float2 px = cell * spacing / max(deriv, 1e-5);
  return 1.0 - saturate(min(px.x, px.y));
}

// 深色 CAD 地面网格：暗填充上叠浅色线，主次网格随视距分级 + 距离淡出。
float4 main(VsOutput input) : SV_Target0 {
  float2 coord = input.world_pos.xz;
  float2 deriv = fwidth(coord);

  // 约 8 像素一条次线；缩小时升到 10 / 100… 避免远景线密成一片灰。
  float world_per_pixel = max(max(deriv.x, deriv.y), 1e-5);
  float lod = log10(max(world_per_pixel * 8.0, 1.0));
  float lod_base = floor(lod);
  float lod_frac = saturate(lod - lod_base);
  float minor_lo = pow(10.0, lod_base);
  float minor_hi = minor_lo * 10.0;

  float minor_strength = lerp(grid_line(coord, minor_lo, deriv),
                              grid_line(coord, minor_hi, deriv), lod_frac);
  float major_strength = lerp(grid_line(coord, minor_lo * 5.0, deriv),
                              grid_line(coord, minor_hi * 5.0, deriv), lod_frac);

  // 淡出随视距缩放，避免缩小后地平线被提前融进天空。
  float view_scale = max(pc.eye_pos_mode.w, 1.0);
  float dist = length(input.world_pos.xz - pc.eye_pos_mode.xz);
  float fade = 1.0 - smoothstep(8.0 * view_scale, 32.0 * view_scale, dist);
  float near_fade = smoothstep(0.0, 0.4 * view_scale, dist);
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
