#include "mesh.hlsli"

#if defined(TAMIAS_VULKAN)
[[vk::binding(0, 0)]] Texture2D albedo_tex;
[[vk::binding(1, 0)]] SamplerState albedo_samp;
[[vk::binding(0, 1)]] Texture2D normal_tex;
[[vk::binding(1, 1)]] SamplerState normal_samp;
[[vk::binding(0, 2)]] TextureCube irradiance_tex;
[[vk::binding(1, 2)]] SamplerState irradiance_samp;
[[vk::binding(0, 3)]] TextureCube prefilter_tex;
[[vk::binding(1, 3)]] SamplerState prefilter_samp;
[[vk::binding(0, 4)]] Texture2D brdf_lut;
[[vk::binding(1, 4)]] SamplerState brdf_samp;
#else
Texture2D albedo_tex : register(t0);
SamplerState albedo_samp : register(s0);
Texture2D normal_tex : register(t1);
SamplerState normal_samp : register(s1);
TextureCube irradiance_tex : register(t2);
SamplerState irradiance_samp : register(s2);
TextureCube prefilter_tex : register(t3);
SamplerState prefilter_samp : register(s3);
Texture2D brdf_lut : register(t4);
SamplerState brdf_samp : register(s4);
#endif

float3 shaded_simple(float3 n, float3 l, float3 base) {
  float ndotl = max(dot(n, l), 0.15);
  return base * ndotl;
}

float3 sample_triplanar_albedo(float3 world_pos, float3 n) {
  float3 wp = world_pos * 2.0;
  float3 blend = abs(n);
  blend = pow(blend, 4.0);
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  float3 cx = albedo_tex.Sample(albedo_samp, wp.zy).rgb;
  float3 cy = albedo_tex.Sample(albedo_samp, wp.xz).rgb;
  float3 cz = albedo_tex.Sample(albedo_samp, wp.xy).rgb;
  return cx * blend.x + cy * blend.y + cz * blend.z;
}

float3 unpack_normal(float3 rgb) {
  return rgb * 2.0 - 1.0;
}

// Whiteout blend of tangent-space normals projected onto world axes.
float3 sample_triplanar_normal(float3 world_pos, float3 n) {
  float3 wp = world_pos * 2.0;
  float3 blend = abs(n);
  blend = pow(blend, 4.0);
  blend = blend / max(blend.x + blend.y + blend.z, 1e-6);
  float3 tx = unpack_normal(normal_tex.Sample(normal_samp, wp.zy).xyz);
  float3 ty = unpack_normal(normal_tex.Sample(normal_samp, wp.xz).xyz);
  float3 tz = unpack_normal(normal_tex.Sample(normal_samp, wp.xy).xyz);
  float3 nx = float3(tx.z, tx.y, tx.x);
  float3 ny = float3(ty.x, ty.z, ty.y);
  float3 nz = float3(tz.x, tz.y, tz.z);
  return normalize(n * float3(blend.z + blend.y, blend.x + blend.z, blend.x + blend.y) +
                   nx * blend.x + ny * blend.y + nz * blend.z);
}

float3 sample_uv_normal(float3 n, float3 world_pos, float2 uv) {
  float3 tnormal = unpack_normal(normal_tex.Sample(normal_samp, uv).xyz);
  float3 dp1 = ddx(world_pos);
  float3 dp2 = ddy(world_pos);
  float2 duv1 = ddx(uv);
  float2 duv2 = ddy(uv);
  float3 dp2perp = cross(dp2, n);
  float3 dp1perp = cross(n, dp1);
  float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
  float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
  float inv = rsqrt(max(dot(t, t) * dot(b, b), 1e-8));
  t *= inv;
  b *= inv;
  return normalize(t * tnormal.x + b * tnormal.y + n * tnormal.z);
}

float4 shaded_realistic(float3 n, float3 l, float3 v, float3 base, float rough, float metal,
                        float opacity) {
  const float PI = 3.14159265;

  float ndotl = max(dot(n, l), 0.0);
  float ndotv = max(dot(n, v), 1e-4);
  float3 h = normalize(l + v);
  float ndoth = max(dot(n, h), 1e-4);
  float vdoth = max(dot(v, h), 1e-4);

  float roughness = clamp(rough, 0.045, 1.0);
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;

  float3 f0 = lerp(float3(0.04, 0.04, 0.04), base, metal);
  float3 F = f0 + (float3(1.0, 1.0, 1.0) - f0) * pow(1.0 - vdoth, 5.0);

  float transmissive = (opacity < 0.999 && metal < 0.5) ? 1.0 : 0.0;
  float3 kd = base * (1.0 - metal);
  if (transmissive > 0.5) {
    kd *= opacity;
  }
  float3 diffuse = (kd / PI) * ndotl * (float3(1.0, 1.0, 1.0) - F);

  float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
  float D = alpha2 / (PI * denom * denom);

  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  float gv = ndotv / (ndotv * (1.0 - k) + k);
  float gl = ndotl / (ndotl * (1.0 - k) + k);
  float G = gv * gl;

  float3 specular = (D * G * F) / max(4.0 * ndotv, 1e-4);

  float3 light_col = float3(1.0, 1.0, 1.0) * pc.lighting.y;
  float3 punctual_diff = diffuse * light_col;
  float3 punctual_spec = specular * light_col;

  float3 irradiance = irradiance_tex.Sample(irradiance_samp, n).rgb;
  float3 R = reflect(-v, n);
  float lod = roughness * pc.lighting.z;
  float3 prefiltered = prefilter_tex.SampleLevel(prefilter_samp, R, lod).rgb;
  float2 env_brdf = brdf_lut.Sample(brdf_samp, float2(ndotv, roughness)).rg;
  float3 spec_ibl = prefiltered * (f0 * env_brdf.x + env_brdf.y);
  float3 diff_ibl = irradiance * kd;

  float3 spec = (punctual_spec + spec_ibl) * pc.lighting.x;
  float3 diff = (punctual_diff + diff_ibl) * pc.lighting.x;
  if (transmissive > 0.5) {
    float3 ldr_spec = spec / (spec + float3(0.85, 0.85, 0.85));
    float3 ldr_diff = diff / (diff + float3(0.85, 0.85, 0.85));
    float a = saturate(opacity + (1.0 - opacity) * pow(1.0 - ndotv, 5.0));
    return float4(ldr_spec + ldr_diff * a, a);
  }
  float3 color = spec + diff;
  color = color / (color + float3(0.85, 0.85, 0.85));
  return float4(color, 1.0);
}

float4 main(VsOutput input) : SV_Target0 {
  if (input.mode > 2.5) {
    float3 c = input.color * pc.color.rgb;
    if (input.selected > 0.5) {
      c = float3(0.35, 0.72, 1.0);
    }
    return float4(c, 1.0);
  }

  if (input.mode < 0.5) {
    float3 wire = float3(0.82, 0.86, 0.92);
    if (input.selected > 0.5) {
      wire = float3(0.35, 0.72, 1.0);
    }
    return float4(wire, 1.0);
  }

  float3 n = normalize(input.normal);
  float3 v = normalize(pc.eye_pos_mode.xyz - input.world_pos);
  if (dot(n, v) < 0.0) {
    discard;
  }

  if (pc.material.w > 0.5) {
    if (pc.lighting.w > 0.5) {
      n = sample_uv_normal(n, input.world_pos, input.uv);
    } else {
      n = sample_triplanar_normal(input.world_pos, n);
    }
    n = normalize(n);
    if (dot(n, v) < 0.0) {
      n = -n;
    }
  }

  float3 l = normalize(pc.light_dir_selected.xyz);
  float3 base = pc.color.rgb * input.color;
  if (pc.material.z > 0.5) {
    if (pc.lighting.w > 0.5) {
      base = albedo_tex.Sample(albedo_samp, input.uv).rgb;
    } else {
      base = sample_triplanar_albedo(input.world_pos, n);
    }
  }
  float rough = pc.material.x;
  float metal = pc.material.y;
  float opacity = saturate(pc.color.w);

  float4 lit;
  if (input.mode > 1.5) {
    lit = shaded_realistic(n, l, v, base, rough, metal, opacity);
  } else {
    lit = float4(shaded_simple(n, l, base), 1.0);
  }

  if (input.selected > 0.5) {
    lit.rgb = lerp(lit.rgb, float3(0.28, 0.62, 1.0), 0.22);
    lit.rgb += float3(0.03, 0.07, 0.14);
  }
  return lit;
}
