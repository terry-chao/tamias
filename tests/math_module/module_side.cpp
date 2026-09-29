// module 侧：只 import tamias.math，不 include 任何 tamias 数学头文件。
// 这份 TU 能编过，本身就说明 BMI 里带的声明是完整可用的。

#include "math_module/parity_probe.h"

import tamias.math;

namespace tamias_math_probe {

#include "math_module/probe_body.inc"

std::array<float, kProbeCount> probe_from_module() {
  return compute_math_probes<ModuleTag>();
}

}  // namespace tamias_math_probe
