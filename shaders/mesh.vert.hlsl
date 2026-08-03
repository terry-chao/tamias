#include "mesh.hlsli"

VsOutput main(VsInput input) {
  VsOutput o;
  float4 world = mul(pc.model, float4(input.position, 1.0));
  o.world_pos = world.xyz;
  // Assume uniform scale; good enough for CAD viewport shading.
  o.normal = mul((float3x3)pc.model, input.normal);
  o.uv = input.uv;
  o.color = input.color;
  o.selected = pc.light_dir_selected.w;
  o.mode = pc.eye_pos_mode.w;
  o.position = mul(pc.mvp, float4(input.position, 1.0));
  return o;
}
