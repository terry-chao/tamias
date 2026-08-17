#include "mesh.hlsli"

float3 shaded_simple(float3 n, float3 l, float3 base) {
  float ndotl = max(dot(n, l), 0.15);
  return base * ndotl;
}

float3 shaded_realistic(float3 n, float3 l, float3 v, float3 base, float rough, float metal) {
  const float PI = 3.14159265;

  float ndotl = max(dot(n, l), 0.0);
  float ndotv = max(dot(n, v), 1e-4);
  float3 h = normalize(l + v);
  float ndoth = max(dot(n, h), 1e-4);

  // 粗糙度下限避免高光无限锐利；alpha = roughness² 是 GGX 惯例。
  float roughness = clamp(rough, 0.045, 1.0);
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;

  // F0：非金属 0.04，金属趋近本体色。
  float3 f0 = lerp(float3(0.04, 0.04, 0.04), base, metal);
  // Schlick Fresnel。
  float3 F = f0 + (float3(1.0, 1.0, 1.0) - f0) * pow(1.0 - ndotv, 5.0);

  // 漫反射：金属无漫反射（颜色全来自高光）；乘 (1-F) 保能量守恒。
  float3 kd = base * (1.0 - metal);
  float3 diffuse = kd * ndotl * (float3(1.0, 1.0, 1.0) - F);

  // GGX 法向分布（D）：高粗糙度下有粗糙表面的擦边长尾。
  float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
  float D = alpha2 / (PI * denom * denom);

  // Smith 几何遮蔽（G），Schlick-GGX 形式：掠射角自然衰减。
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float gv = ndotv / (ndotv * (1.0 - k) + k);
  float gl = ndotl / (ndotl * (1.0 - k) + k);
  float G = gv * gl;

  // 镜面反射（Cook-Torrance）：方向光积分里 NdotL 与 BRDF 分母约掉，只剩 4*NdotV。
  float3 specular = (D * G * F) / max(4.0 * ndotv, 1e-4);

  // 半球环境光（Y-up：朝上=天空，朝下=地面），替代没有 IBL 时的环境项。
  float hemi = saturate(0.5 * n.y + 0.5);
  float3 sky = float3(0.34, 0.38, 0.44);
  float3 ground = float3(0.13, 0.14, 0.15);
  float3 env = lerp(ground, sky, hemi);

  // 环境漫反射（非金属）+ 环境镜面反射（金属靠它显色，rough 越高越模糊越暗）。
  float3 ambient_diffuse = env * kd;
  float3 env_spec_color = lerp(float3(0.04, 0.04, 0.04), base, metal);
  float3 ambient_specular =
      env * env_spec_color * (0.05 + 0.6 * (1.0 - roughness) * (1.0 - roughness));

  float3 color = ambient_diffuse + ambient_specular + diffuse + specular;

  // Reinhard tonemap 保留原有风格，避免 sRGB swapchain 高光溢出。
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
  // Triplanar 采样：世界坐标投影到 X/Y/Z 三平面，按法线加权混合。
  // 无需网格 UV，墙/box/圆柱/导入 BRep 全部适用。
  if (pc.material.z > 0.5) {
    float3 wp = input.world_pos;
    float3 blend = abs(n);
    blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
    float3 cx = albedo_tex.Sample(albedo_samp, wp.zy).rgb;
    float3 cy = albedo_tex.Sample(albedo_samp, wp.xz).rgb;
    float3 cz = albedo_tex.Sample(albedo_samp, wp.xy).rgb;
    base = cx * blend.x + cy * blend.y + cz * blend.z;
  }
  float rough = pc.material.x;
  float metal = pc.material.y;

  float3 lit;
  if (input.mode > 1.5) {
    lit = shaded_realistic(n, l, v, base, rough, metal);
  } else {
    lit = shaded_simple(n, l, base);
  }

  if (input.selected > 0.5) {
    lit = lerp(lit, float3(1.0, 0.75, 0.2), 0.45);
  }
  return float4(lit, 1.0);
}
