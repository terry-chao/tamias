#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/modeling/feature.h"

namespace tamias {

// 造型信息抽象（createGeom）：把特征树（配方）变成几何（三角网）。
// 内核无关：OCCT 是一实现，未来可替换成其它内核。实体层委托它生成几何。
class IGeometryBuilder {
 public:
  virtual ~IGeometryBuilder() = default;
  [[nodiscard]] virtual Result<MeshCpu> build(const FeatureModel& model,
                                              double deflection) const = 0;
};

// 过渡接口：老调用方（Entity::createGeom、各种 rebuild）只想要「特征树 → 网格」。
// 实现转发给默认内核（见 linked_kernels.h）。P3 会把内核显式传进这些调用点，
// 这个全局入口随之删除——不要在新代码里用它。
[[nodiscard]] IGeometryBuilder& geometry_builder();

}  // namespace tamias
