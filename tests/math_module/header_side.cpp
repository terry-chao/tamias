// header 侧：只看头文件，不认识模块。头部 include 顺序与其它 TU 一致。

#include "engine/math/aabb2.h"
#include "engine/math/camera.h"
#include "engine/math/grid.h"
#include "engine/math/math.h"

#include "math_module/parity_probe.h"

namespace tamias_math_probe {

#include "math_module/probe_body.inc"

std::array<float, kProbeCount> probe_from_headers() {
  return compute_math_probes<HeaderTag>();
}

}  // namespace tamias_math_probe
