#include "mesh.hlsli"

// 地面网格顶点：四边形相机锚定，世界坐标由 model 变换给出，传给片元算网格线。
VsOutput main(VsInput input) {
  VsOutput o;
  float4 world = mul(pc.model, float4(input.position, 1.0));
  o.world_pos = world.xyz;
  o.position = mul(pc.mvp, float4(input.position, 1.0));
  o.normal = float3(0.0, 1.0, 0.0);
  o.uv = float2(0.0, 0.0);
  o.selected = 0.0;
  o.mode = 0.0;
  o.color = float3(1.0, 1.0, 1.0);
  return o;
}
