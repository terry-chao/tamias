#include "math_module/parity_probe.h"

#include <algorithm>
#include <cstdio>

namespace tamias_math_probe {

std::string compare_probes() {
  const std::array<float, kProbeCount> headers = probe_from_headers();
  const std::array<float, kProbeCount> module = probe_from_module();

  for (std::size_t i = 0; i < kProbeCount; ++i) {
    const float a = headers[i];
    const float b = module[i];
    const bool both_nan = std::isnan(a) && std::isnan(b);
    const float scale = std::max(1.f, std::max(std::fabs(a), std::fabs(b)));
    if (!both_nan && std::fabs(a - b) > 1e-5f * scale) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "probe[%zu] header=%g module=%g",
                    static_cast<std::size_t>(i), static_cast<double>(a),
                    static_cast<double>(b));
      return buf;
    }
  }
  return {};
}

}  // namespace tamias_math_probe
