// 全屏三角形：顶点已经是 NDC 坐标，直接透传，不做 MVP 变换。
struct VsInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(1)]] float3 normal : NORMAL;
  [[vk::location(2)]] float2 uv : TEXCOORD0;
  [[vk::location(3)]] float3 color : COLOR;
};

struct VsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VsOutput main(VsInput input) {
  VsOutput o;
  o.position = float4(input.position, 1.0);
  o.uv = input.position.xy * 0.5 + 0.5;  // NDC [-1,1] -> [0,1]
  return o;
}
