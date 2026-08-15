#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/modeling/feature.h"

namespace tamias {

#if defined(TAMIAS_HAS_OCCT)

// 求值特征树 → 三角网（内部：特征树 → BRep → tessellate）。
// 求值器假设特征按拓扑序排列（依赖在前），即 add_feature 的插入顺序。
[[nodiscard]] Result<MeshCpu> evaluate_feature_model(const FeatureModel& model,
                                                     double linear_deflection = 0.1);

#endif

}  // namespace tamias
