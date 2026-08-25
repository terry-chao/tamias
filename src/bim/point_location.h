#pragma once

#include "bim/location.h"

namespace tamias {

class PointLocation final : public Location {
 public:
  explicit PointLocation(Vec3 point = {}, std::uint64_t storey_id = 0,
                         double elevation_offset = 0.0)
      : Location(storey_id, elevation_offset), point_(point) {
    point_.y = 0.f;
  }

  [[nodiscard]] LocationKind kind() const override { return LocationKind::Point; }
  [[nodiscard]] std::unique_ptr<Location> clone() const override;
  [[nodiscard]] Mat4 transform(double storey_elevation) const override;

  [[nodiscard]] Vec3 point() const { return point_; }
  void set_point(Vec3 point) {
    point.y = 0.f;
    point_ = point;
  }

 private:
  Vec3 point_{};
};

}  // namespace tamias
