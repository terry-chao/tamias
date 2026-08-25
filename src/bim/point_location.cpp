#include "bim/point_location.h"

namespace tamias {

std::unique_ptr<Location> PointLocation::clone() const {
  return std::make_unique<PointLocation>(*this);
}

Mat4 PointLocation::transform(double storey_elevation) const {
  return translate({point_.x, static_cast<float>(storey_elevation + elevation_offset()),
                    point_.z});
}

}  // namespace tamias
