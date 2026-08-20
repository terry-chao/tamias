#pragma once

#include "engine/document/document.h"
#include "engine/math/camera.h"
#include "engine/math/math.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace tamias {

struct PickHit {
  std::uint64_t node_id = 0;
  std::uint32_t triangle_index = 0;
  float t = 0.f;
};

class Bvh {
 public:
  void build(const Document& doc);
  [[nodiscard]] std::optional<PickHit> closest_hit(
      const Ray& ray, const Document& doc,
      const std::function<bool(std::uint64_t)>& accept = {}) const;

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

// 世界点投到屏幕像素；在相机后面则失败。
[[nodiscard]] bool project_world_to_screen(const Mat4& view_proj, Vec3 world, float width,
                                           float height, float& out_x, float& out_y);

// 框选：window=完全落入矩形，crossing=屏幕包围盒与矩形相交。
[[nodiscard]] std::vector<std::uint64_t> nodes_in_screen_rect(
    const Document& doc, const Mat4& view_proj, float width, float height, float x0, float y0,
    float x1, float y1, bool crossing);

}  // namespace tamias
