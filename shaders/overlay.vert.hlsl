#include "mesh.hlsli"

// 图纸底图顶点：单位四边形（局部 (u,v) ∈ [0,1]²，y = 0）由 pc.model 摆到世界，
// 不参与光照，只把 UV 与透明度交给片元去采图纸贴图。
// 和 grid.vert 一样走 pc.model（不是网格实例矩阵），所以管线不绑实例缓冲。
// 输入输出只声明自己用得到的字段（顶点输入布局是几条管线共用的，用不到的属性
// 在这里不声明——和 sky.vert 一个写法）。
struct OverlayVsInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(2)]] float2 uv : TEXCOORD0;
  [[vk::location(3)]] float3 color : COLOR;
};

struct OverlayVsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
  [[vk::location(1)]] float3 color : COLOR;
  [[vk::location(2)]] float opacity : TEXCOORD1;
};

OverlayVsOutput main(OverlayVsInput input) {
  OverlayVsOutput o;
  o.position = mul(pc.mvp, float4(input.position, 1.0));
  o.uv = input.uv;
  o.color = input.color;
  o.opacity = pc.color.a;
  return o;
}
