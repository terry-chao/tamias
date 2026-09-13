#pragma once

#include "engine/core/result.h"
#include "engine/graphics/mesh.h"
#include "engine/modeling/feature.h"
#include "engine/modeling/kernel/kernel.h"

#include <cstdint>
#include <unordered_map>

namespace tamias {

// 特征树求值（内核无关）。这里是「中间层」：遍历特征、调内核动词、出三角网。
// 唯一的内核依赖是 ModelKernel&，没有任何 OCCT / ACIS 类型。

// 按拓扑序把特征算成体。stop_feature_id != 0 时算到那个特征就停
// （采集指纹、将来的边拾取都要用）。
[[nodiscard]] Result<std::unordered_map<std::uint64_t, BodyRef>> evaluate_feature_bodies(
    const FeatureModel& model, ModelKernel& kernel, std::uint64_t stop_feature_id = 0);

[[nodiscard]] Result<MeshCpu> evaluate_feature_model(const FeatureModel& model,
                                                     ModelKernel& kernel,
                                                     double linear_deflection = 0.1);

// 过渡重载：用进程默认内核（见 linked_kernels.h）。调用方逐步改成显式传内核后删掉。
[[nodiscard]] Result<MeshCpu> evaluate_feature_model(const FeatureModel& model,
                                                     double linear_deflection);

}  // namespace tamias
