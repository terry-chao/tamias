#pragma once

#include "math/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tamias {

// Semantic scene node, arranged as a tree. Each node stores its transform in the
// *parent's* local space (`local_transform`); the world transform and world-space
// bounds are cached and recomputed by Scene::recompute_world(). Geometry is
// referenced by `mesh_asset_id` (owned by Document) — this node holds no GPU or
// render resources, which live on the render side (see render_runtime.h).
struct SceneNode {
  std::uint64_t id = 0;
  std::string name;
  std::uint64_t parent = 0;               // 0 = root (no parent)
  std::vector<std::uint64_t> children;    // derived cache, rebuilt by recompute_world()
  std::uint64_t mesh_asset_id = 0;        // 0 = grouping node (no geometry)
  Mat4 local_transform = Mat4::identity();
  Mat4 world_transform = Mat4::identity();  // cached: parent chain accumulation
  Vec3 color{0.75f, 0.78f, 0.82f};
  Aabb local_bounds{};                      // own geometry in local space
  Aabb world_bounds{};                      // cached: own + subtree in world space
  bool selected = false;
};

class Scene {
 public:
  SceneNode& add_node(SceneNode node) {
    node.id = next_id_++;
    nodes_.push_back(std::move(node));
    return nodes_.back();
  }

  // Insert a node keeping its id (used by document load / history restore).
  SceneNode& insert_node(SceneNode node) {
    if (node.id == 0) {
      node.id = next_id_++;
    } else {
      next_id_ = std::max(next_id_, node.id + 1);
    }
    nodes_.push_back(std::move(node));
    return nodes_.back();
  }

  [[nodiscard]] const std::vector<SceneNode>& nodes() const { return nodes_; }
  [[nodiscard]] std::vector<SceneNode>& nodes() { return nodes_; }

  [[nodiscard]] std::uint64_t next_id() const { return next_id_; }
  void set_next_id(std::uint64_t id) { next_id_ = std::max<std::uint64_t>(1, id); }

  // Reparent a node (parent 0 = make it a root). Rejects self / descendant cycles
  // so recompute_world() can never loop. Call recompute_world() afterwards.
  void set_parent(std::uint64_t child_id, std::uint64_t parent_id);

  // Set a node's parent-local transform. Call recompute_world() afterwards.
  void set_transform(std::uint64_t node_id, Mat4 local);

  // Rebuild the `children` cache from `parent` links, then recompute every node's
  // world transform (top-down) and world bounds (bottom-up). Full recompute:
  // correct and simple; incremental dirty-subtree propagation can come later.
  void recompute_world();

  void clear() {
    nodes_.clear();
    next_id_ = 1;
  }

  void clear_selection() {
    for (auto& n : nodes_) {
      n.selected = false;
    }
  }

  [[nodiscard]] SceneNode* selected_node() {
    for (auto& n : nodes_) {
      if (n.selected) {
        return &n;
      }
    }
    return nullptr;
  }

  [[nodiscard]] const SceneNode* selected_node() const {
    for (const auto& n : nodes_) {
      if (n.selected) {
        return &n;
      }
    }
    return nullptr;
  }

  SceneNode* find(std::uint64_t id) {
    for (auto& n : nodes_) {
      if (n.id == id) {
        return &n;
      }
    }
    return nullptr;
  }

  const SceneNode* find(std::uint64_t id) const {
    for (const auto& n : nodes_) {
      if (n.id == id) {
        return &n;
      }
    }
    return nullptr;
  }

 private:
  std::vector<SceneNode> nodes_;
  std::uint64_t next_id_ = 1;
};

}  // namespace tamias
