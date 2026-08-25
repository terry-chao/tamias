#include "bim/line_location.h"

#include <algorithm>
#include <cmath>

namespace tamias {

std::unique_ptr<Location> LineLocation::clone() const {
  return std::make_unique<LineLocation>(*this);
}

double LineLocation::length() const {
  const Vec3 d = end_ - start_;
  return std::max(std::sqrt(static_cast<double>(d.x * d.x + d.z * d.z)), 1e-3);
}

Mat4 LineLocation::transform(double storey_elevation) const {
  const Vec3 d = end_ - start_;
  const Vec3 mid{(start_.x + end_.x) * 0.5f,
                 static_cast<float>(storey_elevation + elevation_offset()),
                 (start_.z + end_.z) * 0.5f};
  return translate(mid) * rotate_y(std::atan2(d.x, d.z));
}

}  // namespace tamias
