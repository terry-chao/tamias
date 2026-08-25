#pragma once

#include "bim/location_kind.h"
#include "engine/math/math.h"

#include <cstdint>
#include <memory>

namespace tamias {

class Location {
 public:
  virtual ~Location() = default;

  [[nodiscard]] virtual LocationKind kind() const = 0;
  [[nodiscard]] virtual std::unique_ptr<Location> clone() const = 0;
  [[nodiscard]] virtual Mat4 transform(double storey_elevation) const = 0;

  [[nodiscard]] std::uint64_t storey_id() const { return storey_id_; }
  void set_storey_id(std::uint64_t id) { storey_id_ = id; }
  [[nodiscard]] double elevation_offset() const { return elevation_offset_; }
  void set_elevation_offset(double offset) { elevation_offset_ = offset; }

 protected:
  Location(std::uint64_t storey_id = 0, double elevation_offset = 0.0)
      : storey_id_(storey_id), elevation_offset_(elevation_offset) {}

 private:
  std::uint64_t storey_id_ = 0;
  double elevation_offset_ = 0.0;
};

}  // namespace tamias
