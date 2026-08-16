#pragma once

#include "engine/modeling/geom_builder.h"

namespace tamias {

#if defined(TAMIAS_HAS_OCCT)

// 全局几何构建器（createGeom 的默认实现，OCCT 后端）。
[[nodiscard]] IGeometryBuilder& geometry_builder();

#endif

}  // namespace tamias
