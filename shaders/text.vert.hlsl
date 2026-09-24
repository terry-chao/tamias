#include "mesh.hlsli"

// 屏幕空间文字顶点：单位四边形 (0..1)² 由实例里的**像素矩形**摆到屏幕，UV 从字形图集取。
//
// 实例复用 GpuInstance 布局，字段含义由本 shader 解释（见 runtime/text_quad.h）：
//   row0.xy = 矩形左上角（像素，左上原点）   row0.zw = 矩形尺寸（像素）
//   color   = rgb + a（不预乘，片元里乘上去）
//   tex_st  = xy: 图集 UV 缩放，zw: 图集 UV 偏移
// 视口尺寸走 pc.eye_pos_mode.xy——文字不光照、不挑显示模式，那几个字段本来没人用。
struct TextVsInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(2)]] float2 uv : TEXCOORD0;
  [[vk::location(4)]] float4 rect : RECT;
  [[vk::location(7)]] float4 color : COLOR;
  [[vk::location(9)]] float4 tex_st : TEXCOORD1;
};

struct TextVsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float4 color : COLOR;
};

TextVsOutput main(TextVsInput input) {
  TextVsOutput o;
  const float2 viewport = max(pc.eye_pos_mode.xy, float2(1.0, 1.0));
  const float2 pixels = input.rect.xy + input.position.xy * input.rect.zw;
  const float ndc_x = pixels.x / viewport.x * 2.0 - 1.0;
#if defined(TAMIAS_VULKAN)
  // Vulkan 裁剪空间 Y 向下（见 clip_space_correction_matrix）。
  const float ndc_y = pixels.y / viewport.y * 2.0 - 1.0;
#else
  // OpenGL 裁剪空间 Y 向上，而像素 y 向下，要翻一下。
  const float ndc_y = 1.0 - pixels.y / viewport.y * 2.0;
#endif
  o.position = float4(ndc_x, ndc_y, 0.0, 1.0);
  o.uv = input.tex_st.zw + input.uv * input.tex_st.xy;
  o.color = input.color;
  return o;
}
