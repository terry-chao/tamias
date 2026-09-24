#pragma once

#include "engine/render/runtime/gpu_instance.h"

namespace tamias {

// 一个屏幕空间的字形四边形：像素矩形 + 图集 UV + 颜色。
// 坐标是**设备像素**、左上原点——和 project_world_to_screen() 的输出同一套约定。
// 颜色不预乘：片元里乘 alpha（图集是白 + 预乘 alpha，见 GlyphAtlas）。
struct TextQuad {
  float x = 0.f;
  float y = 0.f;
  float width = 0.f;
  float height = 0.f;
  float u0 = 0.f;
  float v0 = 0.f;
  float u1 = 0.f;
  float v1 = 0.f;
  float color[4] = {1.f, 1.f, 1.f, 1.f};
};

// 打包成实例记录。文字实例**复用 GpuInstance 布局**（字段含义由 text.vert 解释），
// 这样四条 RHI 后端都不用动——它们只按 instanced 标志绑那一套固定属性。
inline GpuInstance make_text_instance(const TextQuad& quad) {
  GpuInstance inst{};
  inst.row0[0] = quad.x;
  inst.row0[1] = quad.y;
  inst.row0[2] = quad.width;
  inst.row0[3] = quad.height;
  inst.color[0] = quad.color[0];
  inst.color[1] = quad.color[1];
  inst.color[2] = quad.color[2];
  inst.color[3] = quad.color[3];
  inst.tex_st[0] = quad.u1 - quad.u0;
  inst.tex_st[1] = quad.v1 - quad.v0;
  inst.tex_st[2] = quad.u0;
  inst.tex_st[3] = quad.v0;
  return inst;
}

}  // namespace tamias
