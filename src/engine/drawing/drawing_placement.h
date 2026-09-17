#pragma once

#include "engine/math/aabb2.h"
#include "engine/math/math.h"

#include <algorithm>
#include <cmath>

namespace tamias {

// 一张参考图纸在模型里的摆放：图纸坐标（Y 向上的二维坐标，单位 = 图纸单位）
// → 世界坐标（米，Y 向上）的一次等比缩放 + 绕 Y 旋转 + 平移。
//
// 约定：图纸坐标 (0,0) 落在世界 (offset_x, elevation, offset_z)；图纸 +X 轴是
// 世界 +X 在俯视面上逆时针转 rotation_deg 度后的方向；图纸 +Y 轴（图纸"上"）对应
// 世界 +Z。后者不是随便定的：翻模把图纸 (x, y) 摆成世界 (x, elevation, y)，平面视图
// 又是"屏幕 X = 世界 +X、屏幕上方 = 世界 +Z"（见 docs/DRAWING-TO-BIM.md、
// TurntableCamera::look_plan），底图必须用同一套映射，否则和照图建的模型镜像。
//
// 这一层是纯数据 + 矩阵计算（引擎侧、Qt-free），摆放交互在 app 层，
// 渲染侧只拿到 drawing_plane_transform() 算出的矩阵。
struct DrawingPlacement {
  double scale = 1.0;         // 图纸单位 → 米
  double rotation_deg = 0.0;  // 绕世界 Y 轴，逆时针（俯视）
  double offset_x = 0.0;      // 图纸原点在世界 XZ 上的落点
  double offset_z = 0.0;
  double elevation = 0.0;  // 世界 Y（标高，米）
};

// 图纸在视口里画成一个单位四边形：局部 (u, v) ∈ [0,1]²、y = 0，
// u 沿图纸从左到右，v 沿图纸从上到下（和光栅化出来的图片行方向一致）。
// page 是这一页在图纸坐标里的矩形。返回「单位四边形 → 世界」的变换。
[[nodiscard]] inline Mat4 drawing_plane_transform(const Aabb2& page,
                                                  const DrawingPlacement& placement) {
  const double x0 = page.min_x;
  const double y0 = page.min_y;
  const double w = std::max(1e-6, static_cast<double>(page.width()));
  const double h = std::max(1e-6, static_cast<double>(page.height()));
  const double s = placement.scale > 0.0 ? placement.scale : 1.0;
  const float angle = static_cast<float>(placement.rotation_deg * 3.14159265358979323846 / 180.0);

  // 图片行是从图纸上方往下的：v 对应的图纸 y 是 y0 + (1 - v) * h，而图纸 y 就是世界 z，
  // 所以 z 随 v 增大而减小（-h）。
  const Mat4 to_drawing = translate({static_cast<float>(x0), 0.f, static_cast<float>(y0 + h)}) *
                          scale({static_cast<float>(w), 1.f, -static_cast<float>(h)});
  // 俯视面上逆时针为正：世界 Y 向下看时，+X 转向 +Z 是逆时针，对应 rotate_y(-θ)。
  const Mat4 to_world =
      translate({static_cast<float>(placement.offset_x), static_cast<float>(placement.elevation),
                 static_cast<float>(placement.offset_z)}) *
      rotate_y(-angle) * scale({static_cast<float>(s), static_cast<float>(s), static_cast<float>(s)});
  return to_world * to_drawing;
}

// 摆放后的图纸在俯视面上的范围（世界 XZ；Aabb2 的 y 分量存的是世界 Z）。
[[nodiscard]] inline Aabb2 drawing_plane_footprint(const Aabb2& page,
                                                   const DrawingPlacement& placement) {
  const Mat4 m = drawing_plane_transform(page, placement);
  Aabb2 box{};
  const float corners[4][3] = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 1.f}, {0.f, 0.f, 1.f}};
  for (const auto& corner : corners) {
    const Vec3 world = m * Vec3{corner[0], corner[1], corner[2]};
    box.expand(world.x, world.z);
  }
  return box;
}

// 默认摆放：unit_scale > 0（图纸写了 $INSUNITS）时按它换算成米，
// 否则把整张图纸等比放进 footprint（模型或轴网的俯视范围），并和它中心对齐——
// 没写单位的 DXF 直接按 1:1 摆会大得离谱，先塞进模型范围里至少看得见，
// 再由用户用「图纸设置」调准。
[[nodiscard]] inline DrawingPlacement default_drawing_placement(const Aabb2& page,
                                                                const Aabb2& footprint,
                                                                double elevation, double unit_scale) {
  DrawingPlacement placement;
  placement.elevation = elevation;
  const double w = std::max(1e-6, static_cast<double>(page.width()));
  const double h = std::max(1e-6, static_cast<double>(page.height()));

  if (unit_scale > 0.0) {
    placement.scale = unit_scale;
  } else if (footprint.valid() && footprint.width() > 1e-6f && footprint.height() > 1e-6f) {
    const double scale_x = static_cast<double>(footprint.width()) / w;
    const double scale_y = static_cast<double>(footprint.height()) / h;
    placement.scale = std::min(scale_x, scale_y);
  } else {
    placement.scale = 1.0;
  }

  // 图纸页中心压到 footprint 中心（没有 footprint 就压到世界原点）。
  const double center_x = static_cast<double>(page.min_x) + w * 0.5;
  const double center_y = static_cast<double>(page.min_y) + h * 0.5;
  const double target_x = footprint.valid() ? 0.5 * (footprint.min_x + footprint.max_x) : 0.0;
  const double target_z = footprint.valid() ? 0.5 * (footprint.min_y + footprint.max_y) : 0.0;
  placement.offset_x = target_x - placement.scale * center_x;
  placement.offset_z = target_z - placement.scale * center_y;
  return placement;
}

}  // namespace tamias
