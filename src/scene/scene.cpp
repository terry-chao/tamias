#include "scene/scene.h"

#include <functional>

namespace tamias {
namespace {

// True if `ancestor` is an ancestor of `node` (walking up parent links). Guarded
// against malformed cycles by capping traversal at the node count.
bool is_ancestor(const std::vector<SceneNode>& nodes, std::uint64_t node,
                 std::uint64_t ancestor) {
  std::uint64_t cur = node;
  for (std::size_t guard = 0; guard < nodes.size() && cur != 0; ++guard) {
    if (cur == ancestor) {
      return true;
    }
    const SceneNode* n = nullptr;
    for (const auto& candidate : nodes) {
      if (candidate.id == cur) {
        n = &candidate;
        break;
      }
    }
    if (n == nullptr) {
      break;
    }
    cur = n->parent;
  }
  return false;
}

}  // namespace

void Scene::set_parent(std::uint64_t child_id, std::uint64_t parent_id) {
  if (child_id == 0 || child_id == parent_id) {
    return;
  }
  if (find(child_id) == nullptr) {
    return;
  }
  // Reject making a node its own descendant (would form a cycle).
  if (is_ancestor(nodes_, parent_id, child_id)) {
    return;
  }
  find(child_id)->parent = parent_id;
}

void Scene::set_transform(std::uint64_t node_id, Mat4 local) {
  if (SceneNode* n = find(node_id)) {
    n->local_transform = local;
  }
}

void Scene::recompute_world() {
  // 1. Rebuild the derived children cache from parent links (single source of
  //    truth is `parent`; `children` is only a traversal cache).
  for (auto& n : nodes_) {
    n.children.clear();
  }
  for (const auto& n : nodes_) {
    if (n.parent != 0) {
      if (SceneNode* p = find(n.parent)) {
        p->children.push_back(n.id);
      }
    }
  }

  // 2. Roots = nodes with no (or dangling) parent.
  std::vector<std::uint64_t> roots;
  for (const auto& n : nodes_) {
    if (n.parent == 0 || find(n.parent) == nullptr) {
      roots.push_back(n.id);
    }
  }

  // 3. Top-down world transform accumulation; bottom-up world bounds union.
  std::function<Aabb(std::uint64_t, const Mat4&)> rec =
      [&](std::uint64_t id, const Mat4& parent_world) -> Aabb {
    SceneNode* n = find(id);
    if (n == nullptr) {
      return {};
    }
    n->world_transform = parent_world * n->local_transform;
    Aabb box{};
    if (n->local_bounds.valid()) {
      box = transform_aabb(n->local_bounds, n->world_transform);
    }
    for (std::uint64_t c : n->children) {
      const Aabb child_box = rec(c, n->world_transform);
      if (child_box.valid()) {
        if (!box.valid()) {
          box = child_box;
        } else {
          box.expand(child_box.min);
          box.expand(child_box.max);
        }
      }
    }
    n->world_bounds = box;
    return box;
  };

  for (std::uint64_t r : roots) {
    rec(r, Mat4::identity());
  }
}

}  // namespace tamias
