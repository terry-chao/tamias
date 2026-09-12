#pragma once

#include "engine/math/math.h"

#include <string>

namespace tamias {

// 图纸图层：只保留看图需要的名字与颜色。可见性是视图状态，不存这里。
struct DrawingLayer {
  std::string name;
  Vec3 color{1.f, 1.f, 1.f};
};

}  // namespace tamias
