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

}  // namespace tamias
