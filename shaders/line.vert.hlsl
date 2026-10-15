#include "mesh.hlsli"

// 屏幕空间像素线（overlay 通道的预览线 / 轴网 / 夹点 / 捕捉 / 调试段走这条）。
//
// 一个实例 = 一条**世界坐标线段** + 一个**像素**线宽。顶点把两个端点投影到屏幕，
// 在屏幕空间按法向把线段撑成一个四边形；片元再按「到中线的像素距离」算覆盖率
// （见 line.frag.hlsl）。线宽因此和相机距离无关——拉远拉近都还是那几个像素粗，
// 而硬件 LineList 的线宽被写死在 1 px（vulkan_device.cpp 的 rs.lineWidth）。
//
// 实例复用 GpuInstance 布局（四条 RHI 后端都不用改，和 text.vert 同一个套路），
// 字段含义由本 shader 解释（见 runtime/line_instance.h）：
//   row0 = xyz: 起点（世界）, w: 线宽（像素）
//   row1 = xyz: 终点（世界）, w: 未用
//   color = rgb + a（不预乘，片元里乘上去）
// 视口尺寸走 pc.eye_pos_mode.xy——和文字共用同一个约定（那个字段本来是显示模式的，
// 文字和线条都不挑显示模式）。
//
// 顶点属性里 position.xy 是**单位四边形**的角 (t, side)：
//   t    ∈ {0,1} 沿线参数（0 = 起点，1 = 终点）
//   side ∈ {0,1} 横向（0 → -1，1 → +1）
struct LineVsInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(4)]] float4 inst_row0 : TEXCOORD6;
  [[vk::location(5)]] float4 inst_row1 : TEXCOORD7;
  [[vk::location(7)]] float4 inst_color : TEXCOORD9;
};

struct LineVsOutput {
  float4 position : SV_Position;
  // x = 沿线参数 t，y = 到中线的**像素**距离（带符号），z = 半宽（像素）
  // （不能叫 line：HLSL 里那是几何着色器的图元类型，是关键字。）
  [[vk::location(0)]] float3 edge : TEXCOORD0;
  [[vk::location(1)]] float4 color : COLOR;
};

// w 小于它就认为端点在相机背后（除下去会把点翻到屏幕另一头）。
static const float kNearEps = 1e-4;
// 四边形横向额外撑出去这么多像素，留给片元算边缘覆盖率（抗锯齿那 1 px）。
static const float kFeather = 1.0;

// 裁剪空间 → 像素（左上原点）。两个后端的 Y 约定不同，和 text.vert.hlsl 同一处写法：
// Vulkan 的裁剪空间 Y 已由 clip_space_correction_matrix 翻成向下，GL 仍是向上。
float2 pixel_from_ndc(float2 ndc, float2 viewport) {
#if defined(TAMIAS_VULKAN)
  return float2((ndc.x * 0.5 + 0.5) * viewport.x, (ndc.y * 0.5 + 0.5) * viewport.y);
#else
  return float2((ndc.x * 0.5 + 0.5) * viewport.x, (0.5 - ndc.y * 0.5) * viewport.y);
#endif
}

float2 ndc_from_pixel(float2 pixel, float2 viewport) {
#if defined(TAMIAS_VULKAN)
  return float2(pixel.x / viewport.x * 2.0 - 1.0, pixel.y / viewport.y * 2.0 - 1.0);
#else
  return float2(pixel.x / viewport.x * 2.0 - 1.0, 1.0 - pixel.y / viewport.y * 2.0);
#endif
}

LineVsOutput main(LineVsInput input) {
  LineVsOutput o;
  const float2 viewport = max(pc.eye_pos_mode.xy, float2(1.0, 1.0));
  const float t = input.position.x;
  const float side = input.position.y * 2.0 - 1.0;
  const float half_width = max(input.inst_row0.w, 0.5) * 0.5;

  float4 clip_a = mul(pc.mvp, float4(input.inst_row0.xyz, 1.0));
  float4 clip_b = mul(pc.mvp, float4(input.inst_row1.xyz, 1.0));
  // 端点跑到相机背后：整段丢掉，或把背后那一端压到近平面上，让线看起来是被裁掉的。
  if (clip_a.w < kNearEps && clip_b.w < kNearEps) {
    o.position = float4(2.0, 2.0, 2.0, 1.0);  // 视锥外，光栅化阶段整条丢掉
    o.edge = float3(t, 0.0, half_width);
    o.color = input.inst_color;
    return o;
  }
  if (clip_a.w < kNearEps) {
    clip_a = lerp(clip_a, clip_b, (kNearEps - clip_a.w) / (clip_b.w - clip_a.w));
  } else if (clip_b.w < kNearEps) {
    clip_b = lerp(clip_b, clip_a, (kNearEps - clip_b.w) / (clip_a.w - clip_b.w));
  }

  const float2 a_px = pixel_from_ndc(clip_a.xy / clip_a.w, viewport);
  const float2 b_px = pixel_from_ndc(clip_b.xy / clip_b.w, viewport);
  // 线段在屏幕上退化成一个点时给个默认方向，免得归一化出 NaN。
  const float2 delta = b_px - a_px;
  const float len = length(delta);
  const float2 dir = len > 1e-4 ? delta / len : float2(1.0, 0.0);
  const float2 normal = float2(-dir.y, dir.x);

  const float offset = side * (half_width + kFeather);
  const float2 px = lerp(a_px, b_px, t) + normal * offset;

  // 深度按沿线参数线性插值；w 人为置 1，所以后面的变化量按屏幕空间线性插值就对了
  // （这条管线本来不测深度，值是给将来的 depth-tested 变体留的）。
  const float z_over_w = lerp(clip_a.z / clip_a.w, clip_b.z / clip_b.w, t);
  const float2 ndc = ndc_from_pixel(px, viewport);
  o.position = float4(ndc.x, ndc.y, z_over_w, 1.0);
  o.edge = float3(t, offset, half_width);
  o.color = input.inst_color;
  return o;
}
