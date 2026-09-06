#pragma once

#include "engine/math/math.h"

namespace tamias {

// 视口调试：选中网格顶点的世界系标记（位置 / 法线 / 顶点色 / UV）。
struct DebugVertexOverlay {
  int index = -1;
  Vec3 world{};
  Vec3 normal{0.f, 1.f, 0.f};
  Vec3 color{1.f, 1.f, 1.f};
  Vec2 uv{};
};

// 法线走逆转置，避免非均匀缩放把方向拧歪。
inline Vec3 transform_normal_affine(const Mat4& m, Vec3 n) {
  const Mat4 inv = invert_affine(m);
  return normalize({inv(0, 0) * n.x + inv(1, 0) * n.y + inv(2, 0) * n.z,
                    inv(0, 1) * n.x + inv(1, 1) * n.y + inv(2, 1) * n.z,
                    inv(0, 2) * n.x + inv(1, 2) * n.y + inv(2, 2) * n.z});
}

}  // namespace tamias
