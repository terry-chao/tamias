#include "picking.h"

#include <algorithm>
#include <limits>

namespace tamias {
namespace {

// 把世界空间射线逆变换到节点的局部空间。网格顶点存在局部空间，而拾取射线是世界空间，
// 两者不能直接相交——否则任何带 transform 的节点（墙/非原点的盒子/圆柱）都会漏选。
// 当前场景变换都是刚体（平移 + 旋转，无缩放），所以逆变换 = Rᵀ·(p - t)；方向只旋转
// 不平移，且刚体保距，交点 t 不变。
Ray to_local_ray(const Ray& ray, const Mat4& m) {
  const Vec3 t{m(0, 3), m(1, 3), m(2, 3)};
  const auto rotate = [&](Vec3 v) {
    // Rᵀ · v（R 是 m 左上 3×3，列主序下 R(row,col) = m(row,col)）。
    return Vec3{m(0, 0) * v.x + m(1, 0) * v.y + m(2, 0) * v.z,
                m(0, 1) * v.x + m(1, 1) * v.y + m(2, 1) * v.z,
                m(0, 2) * v.x + m(1, 2) * v.y + m(2, 2) * v.z};
  };
  return {rotate(ray.origin - t), rotate(ray.direction)};
}

}  // namespace

std::int32_t Bvh::build_range(std::vector<std::uint64_t>& ids, const Document& doc, int begin,
                              int end) {
  Node node;
  for (int i = begin; i < end; ++i) {
    if (const auto* sn = doc.scene().find(ids[static_cast<std::size_t>(i)])) {
      if (!node.bounds.valid()) {
        node.bounds = sn->world_bounds;
      } else {
        node.bounds.expand(sn->world_bounds.min);
        node.bounds.expand(sn->world_bounds.max);
      }
    }
  }
  if (end - begin == 1) {
    node.leaf = true;
    node.scene_node_id = ids[static_cast<std::size_t>(begin)];
    nodes_.push_back(node);
    return static_cast<std::int32_t>(nodes_.size() - 1);
  }

  const Vec3 extent = node.bounds.extent();
  int axis = 0;
  if (extent.y > extent.x && extent.y >= extent.z) {
    axis = 1;
  } else if (extent.z > extent.x && extent.z >= extent.y) {
    axis = 2;
  }
  const int mid = (begin + end) / 2;
  std::nth_element(ids.begin() + begin, ids.begin() + mid, ids.begin() + end,
                   [&](std::uint64_t a, std::uint64_t b) {
                     const auto* na = doc.scene().find(a);
                     const auto* nb = doc.scene().find(b);
                     const Vec3 ca = na ? na->world_bounds.center() : Vec3{};
                     const Vec3 cb = nb ? nb->world_bounds.center() : Vec3{};
                     const float* pa = &ca.x;
                     const float* pb = &cb.x;
                     return pa[axis] < pb[axis];
                   });

  nodes_.push_back(node);
  const auto self = static_cast<std::int32_t>(nodes_.size() - 1);
  const auto left = build_range(ids, doc, begin, mid);
  const auto right = build_range(ids, doc, mid, end);
  nodes_[static_cast<std::size_t>(self)].left = left;
  nodes_[static_cast<std::size_t>(self)].right = right;
  return self;
}

void Bvh::build(const Document& doc) {
  nodes_.clear();
  root_ = -1;
  std::vector<std::uint64_t> ids;
  for (const auto& n : doc.scene().nodes()) {
    if (n.mesh_asset_id != 0 && n.world_bounds.valid()) {
      ids.push_back(n.id);
    }
  }
  if (ids.empty()) {
    return;
  }
  root_ = build_range(ids, doc, 0, static_cast<int>(ids.size()));
}

std::optional<PickHit> Bvh::closest_hit(const Ray& ray, const Document& doc) const {
  if (root_ < 0) {
    return std::nullopt;
  }
  std::optional<PickHit> best;
  float best_t = std::numeric_limits<float>::max();
  std::vector<std::int32_t> stack;
  stack.push_back(root_);
  while (!stack.empty()) {
    const auto idx = stack.back();
    stack.pop_back();
    const Node& node = nodes_[static_cast<std::size_t>(idx)];
    float t_box = 0.f;
    if (!intersect_aabb(ray, node.bounds, t_box) || t_box > best_t) {
      continue;
    }
    if (node.leaf) {
      const SceneNode* sn = doc.scene().find(node.scene_node_id);
      if (!sn) {
        continue;
      }
      const MeshAsset* asset = doc.mesh(sn->mesh_asset_id);
      if (!asset) {
        continue;
      }
      const Ray local_ray = to_local_ray(ray, sn->world_transform);
      const auto& mesh = asset->cpu;
      for (std::uint32_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const auto i0 = mesh.indices[t];
        const auto i1 = mesh.indices[t + 1];
        const auto i2 = mesh.indices[t + 2];
        float hit_t = 0.f;
        if (intersect_triangle(local_ray, mesh.vertices[i0].position, mesh.vertices[i1].position,
                               mesh.vertices[i2].position, hit_t) &&
            hit_t < best_t) {
          best_t = hit_t;
          best = PickHit{sn->id, t / 3, hit_t};
        }
      }
      continue;
    }
    if (node.left >= 0) {
      stack.push_back(node.left);
    }
    if (node.right >= 0) {
      stack.push_back(node.right);
    }
  }
  return best;
}

Ray camera_ray(const TurntableCamera& camera, float aspect, float mouse_x, float mouse_y,
               float width, float height) {
  const float ndc_x = (2.f * mouse_x / width) - 1.f;
  const float ndc_y = 1.f - (2.f * mouse_y / height);
  const Vec3 eye = camera.eye_position();
  const Vec3 forward = normalize(camera.target() - eye);
  const Vec3 right = normalize(cross(forward, {0.f, 1.f, 0.f}));
  const Vec3 up = cross(right, forward);
  const Mat4 proj = camera.proj_matrix(aspect);
  const float tanx = ndc_x / proj(0, 0);
  const float tany = ndc_y / proj(1, 1);
  Ray ray;
  ray.origin = eye;
  ray.direction = normalize(right * tanx + up * tany + forward);
  return ray;
}

}  // namespace tamias
