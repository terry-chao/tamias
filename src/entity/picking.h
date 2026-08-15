#pragma once

#include "entity/document.h"
#include "engine/math/camera.h"
#include "engine/math/math.h"

#include <cstdint>
#include <optional>

namespace tamias {

struct PickHit {
  std::uint64_t node_id = 0;
  std::uint32_t triangle_index = 0;
  float t = 0.f;
};

class Bvh {
 public:
  void build(const Document& doc);
  [[nodiscard]] std::optional<PickHit> closest_hit(const Ray& ray, const Document& doc) const;

 private:
  struct Node {
    Aabb bounds;
    std::int32_t left = -1;
    std::int32_t right = -1;
    std::uint64_t scene_node_id = 0;
    bool leaf = false;
  };
  std::vector<Node> nodes_;
  std::int32_t root_ = -1;

  std::int32_t build_range(std::vector<std::uint64_t>& ids, const Document& doc, int begin,
                           int end);
};

Ray camera_ray(const TurntableCamera& camera, float aspect, float mouse_x, float mouse_y,
               float width, float height);

}  // namespace tamias
