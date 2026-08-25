#pragma once

#include "bim/location.h"

namespace tamias {

class LineLocation final : public Location {
 public:
  LineLocation(Vec3 start = {}, Vec3 end = {0.f, 0.f, 1.f}, std::uint64_t storey_id = 0,
               double elevation_offset = 0.0)
      : Location(storey_id, elevation_offset), start_(start), end_(end) {
    start_.y = 0.f;
    end_.y = 0.f;
  }

  [[nodiscard]] LocationKind kind() const override { return LocationKind::Line; }
  [[nodiscard]] std::unique_ptr<Location> clone() const override;
  [[nodiscard]] Mat4 transform(double storey_elevation) const override;

  [[nodiscard]] Vec3 start() const { return start_; }
  [[nodiscard]] Vec3 end() const { return end_; }
  void set_start(Vec3 start) {
    start.y = 0.f;
    start_ = start;
  }
  void set_end(Vec3 end) {
    end.y = 0.f;
    end_ = end;
  }
  [[nodiscard]] double length() const;

 private:
  Vec3 start_{};
  Vec3 end_{0.f, 0.f, 1.f};
};

}  // namespace tamias
