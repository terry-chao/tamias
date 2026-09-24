#if defined(TAMIAS_VULKAN)
[[vk::binding(0, 0)]] Texture2D atlas_tex;
[[vk::binding(1, 0)]] SamplerState atlas_samp;
#else
Texture2D atlas_tex : register(t0);
SamplerState atlas_samp : register(s0);
#endif

struct TextVsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float4 color : COLOR;
};

// 字形图集是「白 + 预乘 alpha」，覆盖度就在 alpha 里。
// 输出同样预乘（rgb × alpha），和管线里的 ONE / ONE_MINUS_SRC_ALPHA 对齐。
float4 main(TextVsOutput input) : SV_Target0 {
  const float coverage = atlas_tex.Sample(atlas_samp, input.uv).a;
  const float alpha = saturate(coverage * input.color.a);
  return float4(input.color.rgb * alpha, alpha);
}
