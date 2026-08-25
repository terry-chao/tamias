#include "bim/surface_location.h"

#include <cmath>

namespace tamias {

SurfaceLocation::SurfaceLocation(Vec3 origin, Vec3 x_axis, std::uint64_t storey_id,
                                 double elevation_offset)
    : Location(storey_id, elevation_offset), origin_(origin) {
  origin_.y = 0.f;
  set_x_axis(x_axis);
}

void SurfaceLocation::set_x_axis(Vec3 axis) {
  const float len = std::sqrt(axis.x * axis.x + axis.z * axis.z);
  x_axis_ = len > 1e-6f ? Vec3{axis.x / len, 0.f, axis.z / len}
                         : Vec3{1.f, 0.f, 0.f};
}

std::unique_ptr<Location> SurfaceLocation::clone() const {
  return std::make_unique<SurfaceLocation>(*this);
}

Mat4 SurfaceLocation::transform(double storey_elevation) const {
  const Vec3 world_origin{origin_.x,
                          static_cast<float>(storey_elevation + elevation_offset()),
                          origin_.z};
  const float yaw = std::atan2(-x_axis_.z, x_axis_.x);
  return translate(world_origin) * rotate_y(yaw);
}

}  // namespace tamias
