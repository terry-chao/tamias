#include "mesh.hlsli"

struct MeshVsInput {
  [[vk::location(0)]] float3 position : POSITION;
  [[vk::location(1)]] float3 normal : NORMAL;
  [[vk::location(2)]] float2 uv : TEXCOORD0;
  [[vk::location(3)]] float3 color : COLOR;
  [[vk::location(4)]] float4 inst_row0 : TEXCOORD6;
  [[vk::location(5)]] float4 inst_row1 : TEXCOORD7;
  [[vk::location(6)]] float4 inst_row2 : TEXCOORD8;
  [[vk::location(7)]] float4 inst_color : TEXCOORD9;
  [[vk::location(8)]] float4 inst_material : TEXCOORD10;
  [[vk::location(9)]] float4 inst_tex_st : TEXCOORD11;
};

VsOutput main(MeshVsInput input) {
  VsOutput o;
  float4 hp = float4(input.position, 1.0);
  float3 world = float3(dot(input.inst_row0, hp), dot(input.inst_row1, hp),
                        dot(input.inst_row2, hp));
  o.world_pos = world;
  o.normal = float3(dot(input.inst_row0.xyz, input.normal),
                    dot(input.inst_row1.xyz, input.normal),
                    dot(input.inst_row2.xyz, input.normal));
  o.uv = input.uv * input.inst_tex_st.xy + input.inst_tex_st.zw;
  o.color = input.inst_color.rgb * input.color;
  o.selected = input.inst_material.z;
  o.mode = pc.eye_pos_mode.w;
  o.rough_metal = input.inst_material.xy;
  o.opacity = input.inst_color.a;
  o.world_scale = input.inst_material.w;
  // pc.mvp is view-projection; world comes from the instance row.
  o.position = mul(pc.mvp, float4(world, 1.0));
  return o;
}
