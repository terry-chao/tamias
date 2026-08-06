#pragma once

#include "math/math.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace tamias {

struct SceneNode {
  std::uint64_t id = 0;
  std::string name;
  std::uint64_t mesh_asset_id = 0;
  std::uint64_t gpu_mesh_id = 0;
  Mat4 transform = Mat4::identity();
  Vec3 color{0.75f, 0.78f, 0.82f};
  Aabb world_bounds{};
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
