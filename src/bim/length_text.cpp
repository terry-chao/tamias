#include "bim/length_text.h"

#include <cmath>
#include <cstdio>

namespace tamias {

std::string format_elevation(double meters) {
  // 先归整到毫米再判正负：0.0004 应该是 "±0.000" 而不是 "-0.000"。
  const double millimetres = std::round(meters * 1000.0);
  const bool negative = millimetres < 0.0;
  const double magnitude = std::fabs(millimetres) / 1000.0;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%s%.3f",
                negative ? "-" : (millimetres == 0.0 ? "±" : "+"), magnitude);
  return buffer;
}

std::string format_distance(double meters) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", meters);
  return buffer;
}

}  // namespace tamias
