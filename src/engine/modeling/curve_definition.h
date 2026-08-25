#pragma once

#include "engine/math/math.h"
#include "engine/modeling/curve_kind.h"

#include <vector>

namespace tamias {

struct CurveDefinition {
  CurveKind kind = CurveKind::Unknown;
  std::vector<Vec3> points;
  std::vector<double> weights;
  int degree = 0;
};

}  // namespace tamias
