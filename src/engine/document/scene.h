#pragma once

#include "engine/math/math.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
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
    bump_generation();
    mark_dirty(node.id);
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
    bump_generation();
    mark_dirty(node.id);
    return nodes_.back();
  }

  [[nodiscard]] const std::vector<SceneNode>& nodes() const { return nodes_; }
  [[nodiscard]] std::vector<SceneNode>& nodes() { return nodes_; }

  [[nodiscard]] std::uint64_t next_id() const { return next_id_; }
  void set_next_id(std::uint64_t id) { next_id_ = std::max<std::uint64_t>(1, id); }

  // ===== 渲染增量同步的脏标记 =====
  //
  // 语义树是层级的唯一真相源；渲染侧场景图是它的绘制投影。每次 mutator 变更后
  // generation 递增，并把受影响节点按当前代次盖章记入 dirty history。消费方
  // （每帧提交 FrameSubmission 的 app）记录自己上次提交的代次，用
  // dirty_since(last) 取“比 last 新”的脏节点 id，交给渲染线程增量更新。
  // 多消费者安全：不取走即清，各自用游标。

  [[nodiscard]] std::uint64_t generation() const { return generation_; }

  // 代次大于 `since` 的所有变更节点（去重）。代次从 1 开始，0 表示“全部”。
  [[nodiscard]] std::vector<std::uint64_t> dirty_since(std::uint64_t since) const {
    std::unordered_set<std::uint64_t> ids;
    for (const auto& r : dirty_history_) {
      if (r.generation > since) {
        ids.insert(r.node_id);
      }
    }
    return {ids.begin(), ids.end()};
  }

  // 标记单个节点脏（当前代次）。变换/换父用 mark_subtree_dirty（子孙世界矩阵
  // 也会变）；选中/材质等只影响节点自身时用本函数。
  void mark_dirty(std::uint64_t node_id);

  // 标记节点及其全部后代脏（自顶向下世界矩阵累积时，父变则子树全变）。
  void mark_subtree_dirty(std::uint64_t node_id);

  void bump_generation() { ++generation_; }

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
    dirty_history_.clear();
    generation_ = 1;
  }

  // 删除指定节点（供命令撤销用）。
  void remove_node(std::uint64_t id) {
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
      if (it->id == id) {
        nodes_.erase(it);
        bump_generation();
        mark_dirty(id);
        return;
      }
    }
  }

  void clear_selection() {
    const bool any = std::any_of(nodes_.begin(), nodes_.end(),
                                 [](const SceneNode& n) { return n.selected; });
    if (!any) {
      return;
    }
    bump_generation();
    for (auto& n : nodes_) {
      if (n.selected) {
        n.selected = false;
        mark_dirty(n.id);
      }
    }
  }

  [[nodiscard]] std::vector<std::uint64_t> selected_ids() const {
    std::vector<std::uint64_t> ids;
    for (const auto& n : nodes_) {
      if (n.selected) {
        ids.push_back(n.id);
      }
    }
    return ids;
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
  struct DirtyRecord {
    std::uint64_t generation = 0;
    std::uint64_t node_id = 0;
  };

  std::vector<SceneNode> nodes_;
  std::uint64_t next_id_ = 1;
  std::uint64_t generation_ = 1;
  std::vector<DirtyRecord> dirty_history_;
};

}  // namespace tamias
