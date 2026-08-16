#include "mesh.hlsli"

float3 shaded_simple(float3 n, float3 l, float3 base) {
  float ndotl = max(dot(n, l), 0.15);
  return base * ndotl;
}

float3 shaded_realistic(float3 n, float3 l, float3 v, float3 base) {
  // Hemisphere ambient (Z-up)
  float hemi = saturate(0.5 * n.z + 0.5);
  float3 ambient = lerp(float3(0.06, 0.07, 0.09), float3(0.28, 0.30, 0.34), hemi) * base;

  float ndotl = max(dot(n, l), 0.0);
  float3 diffuse = base * ndotl;

  float3 h = normalize(l + v);
  float spec = pow(max(dot(n, h), 0.0), 48.0);
  float fresnel = pow(1.0 - max(dot(n, v), 0.0), 3.0);
  float3 specular = float3(0.95, 0.97, 1.0) * spec * (0.25 + 0.55 * fresnel);

  // Soft fill from opposite side so cavities stay readable.
  float fill = max(dot(n, normalize(float3(-l.x, -l.y, 0.35))), 0.0) * 0.18;
  float3 color = ambient + diffuse * 0.9 + specular + base * fill;

  // Mild Reinhard keep highlights from clipping in sRGB swapchain.
  return color / (color + float3(0.85, 0.85, 0.85));
}

float4 main(VsOutput input) : SV_Target0 {
  // 无光照彩色线条（坐标轴/overlay），mode == 3。
  if (input.mode > 2.5) {
    return float4(input.color, 1.0);
  }

  // Wireframe: flat dark edges, no lighting (mode == 0) — 深色在浅色天空下更清晰。
  if (input.mode < 0.5) {
    float3 wire = float3(0.15, 0.17, 0.20);
    if (input.selected > 0.5) {
      wire = float3(1.0, 0.55, 0.10);
    }
    return float4(wire, 1.0);
  }

  float3 n = normalize(input.normal);
  float3 v = normalize(pc.eye_pos_mode.xyz - input.world_pos);
  // Opaque solids: drop fragments whose geometric normal faces away from the camera.
  // Combined with depth testing this hides the interior without depending on GPU
  // winding / frontFace (which is fragile with Vulkan Y clip correction).
  if (dot(n, v) < 0.0) {
    discard;
  }

  float3 l = normalize(pc.light_dir_selected.xyz);
  float3 base = pc.color.rgb * input.color;

  float3 lit;
  if (input.mode > 1.5) {
    lit = shaded_realistic(n, l, v, base);
  } else {
    lit = shaded_simple(n, l, base);
  }

  if (input.selected > 0.5) {
    lit = lerp(lit, float3(1.0, 0.75, 0.2), 0.45);
  }
  return float4(lit, 1.0);
}
