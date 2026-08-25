#pragma once

#include "bim/location.h"

namespace tamias {

class SurfaceLocation final : public Location {
 public:
  explicit SurfaceLocation(Vec3 origin = {}, Vec3 x_axis = {1.f, 0.f, 0.f},
                           std::uint64_t storey_id = 0, double elevation_offset = 0.0);

  [[nodiscard]] LocationKind kind() const override { return LocationKind::Surface; }
  [[nodiscard]] std::unique_ptr<Location> clone() const override;
  [[nodiscard]] Mat4 transform(double storey_elevation) const override;

  [[nodiscard]] Vec3 origin() const { return origin_; }
  void set_origin(Vec3 origin) {
    origin.y = 0.f;
    origin_ = origin;
  }
  [[nodiscard]] Vec3 x_axis() const { return x_axis_; }
  void set_x_axis(Vec3 axis);

 private:
  Vec3 origin_{};
  Vec3 x_axis_{1.f, 0.f, 0.f};
};

}  // namespace tamias
