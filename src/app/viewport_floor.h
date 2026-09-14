#pragma once

#include "bim/wall_size.h"
#include "engine/document/document.h"
#include "entity/core/entity.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace tamias {

// 视口里的"楼层带"（Y 轴向上）：有楼层表就按楼层表的标高／层高算，没有楼层表
// （老模型）就按几何标高临时分层。它是**视图过滤**用的，不写回文档。
// 构件属于哪层优先看 Location 的楼层（BIM 语义），楼层带只兜底，见 viewport_floor_allows。
struct ViewportFloor {
  std::string label;
  std::uint64_t storey_id = 0;  // 0 = 没有楼层记录，按几何标高临时分层
  float y_min = 0.f;
  float y_max = 0.f;
  float height = static_cast<float>(kDefaultWallHeight);  // 层高（米）
  bool mezzanine = false;
  bool derived = false;  // true = 由几何推出来的临时层（模型里还没有楼层）
};

inline bool viewport_floor_contains(const ViewportFloor& floor, const Aabb& bounds) {
  if (!bounds.valid()) {
    return false;
  }
  return bounds.max.y > floor.y_min && bounds.min.y < floor.y_max;
}

// 按楼层过滤时这个节点算不算可见？hidden = 被隐藏的楼层下标（空 = 全可见）。
// 归属优先看 BIM 语义：Location 指到某个真实楼层的构件只看自己那层——在 1 楼画的
// **顶板**压在 2 楼标高上，它仍然是 1 楼的东西，不该掉进 2 楼。
// 没有楼层归属的（未归属构件 / 导入网格 / 按几何临时分层的模型）退回几何楼层带：
// 碰到任意一个可见的带就还看得见（跨层构件不会因为少碰一层就消失）。
inline bool viewport_floor_allows(const std::vector<ViewportFloor>& floors,
                                  std::uint64_t storey_id, const Aabb& world_bounds,
                                  const std::unordered_set<int>& hidden) {
  if (hidden.empty()) {
    return true;
  }
  if (storey_id != 0) {
    for (std::size_t i = 0; i < floors.size(); ++i) {
      if (floors[i].storey_id == storey_id) {
        return hidden.count(static_cast<int>(i)) == 0;
      }
    }
  }
  for (std::size_t i = 0; i < floors.size(); ++i) {
    if (hidden.count(static_cast<int>(i)) == 0 &&
        viewport_floor_contains(floors[i], world_bounds)) {
      return true;
    }
  }
  return false;
}

inline bool viewport_floor_anchor_kind(const Entity* entity) {
  if (entity == nullptr) {
    return true;  // imported mesh
  }
  switch (entity->kind()) {
    case EntityKind::Wall:
    case EntityKind::Column:
    case EntityKind::Slab:
    case EntityKind::Beam:
    case EntityKind::Box:
    case EntityKind::Cylinder:
    case EntityKind::StructuralWall:
    case EntityKind::Foundation:
    case EntityKind::CurtainWall:
      return true;
    default:
      return false;
  }
}

inline std::vector<ViewportFloor> infer_viewport_floors(const Document& doc) {
  if (!doc.bim().storeys().empty()) {
    std::vector<const Storey*> storeys;
    storeys.reserve(doc.bim().storeys().size());
    for (const Storey& storey : doc.bim().storeys()) {
      storeys.push_back(&storey);
    }
    std::sort(storeys.begin(), storeys.end(),
              [](const Storey* a, const Storey* b) { return a->elevation < b->elevation; });
    std::vector<ViewportFloor> floors;
    floors.reserve(storeys.size());
    for (std::size_t i = 0; i < storeys.size(); ++i) {
      const double height = storeys[i]->height > 0.0 ? storeys[i]->height : kDefaultWallHeight;
      ViewportFloor floor;
      floor.label = storeys[i]->name;
      floor.storey_id = storeys[i]->id;
      floor.y_min = static_cast<float>(storeys[i]->elevation);
      floor.height = static_cast<float>(height);
      floor.mezzanine = storeys[i]->mezzanine;
      floor.y_max =
          i + 1 < storeys.size()
              ? static_cast<float>(storeys[i + 1]->elevation)
              : floor.y_min + static_cast<float>(height);
      if (floor.y_max <= floor.y_min + 0.05f) {
        floor.y_max = floor.y_min + static_cast<float>(height);
      }
      floors.push_back(std::move(floor));
    }
    return floors;
  }

  std::vector<float> elevs;
  for (const auto& node : doc.scene().nodes()) {
    if (node.mesh_asset_id == 0 || !node.world_bounds.valid()) {
      continue;
    }
    if (!viewport_floor_anchor_kind(doc.entity(node.id))) {
      continue;
    }
    elevs.push_back(node.world_bounds.min.y);
  }
  if (elevs.empty()) {
    return {};
  }
  std::sort(elevs.begin(), elevs.end());

  std::vector<float> clustered;
  constexpr float kMerge = 0.4f;
  for (float y : elevs) {
    if (clustered.empty() || y - clustered.back() > kMerge) {
      clustered.push_back(y);
    }
  }

  std::vector<ViewportFloor> floors;
  floors.reserve(clustered.size());
  for (std::size_t i = 0; i < clustered.size(); ++i) {
    ViewportFloor floor;
    floor.label = std::to_string(i + 1) + "F";
    floor.height = static_cast<float>(kDefaultWallHeight);
    floor.derived = true;
    floor.y_min = clustered[i];
    floor.y_max = (i + 1 < clustered.size())
                      ? clustered[i + 1]
                      : clustered[i] + static_cast<float>(kDefaultWallHeight);
    if (floor.y_max <= floor.y_min + 0.05f) {
      floor.y_max = floor.y_min + static_cast<float>(kDefaultWallHeight);
    }
    floors.push_back(floor);
  }
  return floors;
}

}  // namespace tamias
