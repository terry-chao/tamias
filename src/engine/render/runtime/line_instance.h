#pragma once

#include "engine/render/runtime/gpu_instance.h"

namespace tamias {

// 一条**屏幕空间像素线**：世界坐标两端点 + 线宽（设备像素）+ 颜色。
// 覆盖的通道见 docs/RENDERING.md §7：预览线 / 草图曲线 / 贝塞尔控制多边形与控制点 /
// 夹点 / 捕捉点 / 轴网 / 调试段。屏幕空间的只有**线宽**，端点还是世界坐标——所以
// 拉远拉近粗细不变，位置照旧跟着相机走。
struct OverlayLine {
  Vec3 start{};
  Vec3 end{};
  Vec3 color{0.75f, 0.78f, 0.82f};
  float width_px = 1.5f;
};

// 打包成实例记录。和文字实例一样**复用 GpuInstance 布局**（字段含义由 line.vert.hlsl
// 解释），四条 RHI 后端都不用按新属性改：
//   row0 = xyz: 起点（世界）, w: 线宽（像素）
//   row1 = xyz: 终点（世界）, w: 未用
//   color = rgb + a（不预乘，片元里乘上去）
// row2 / material / tex_st 这条通路不用，保持默认值。
inline GpuInstance make_line_instance(const OverlayLine& line) {
  GpuInstance inst{};
  inst.row0[0] = line.start.x;
  inst.row0[1] = line.start.y;
  inst.row0[2] = line.start.z;
  inst.row0[3] = line.width_px;
  inst.row1[0] = line.end.x;
  inst.row1[1] = line.end.y;
  inst.row1[2] = line.end.z;
  inst.color[0] = line.color.x;
  inst.color[1] = line.color.y;
  inst.color[2] = line.color.z;
  inst.color[3] = 1.f;
  return inst;
}

}  // namespace tamias
