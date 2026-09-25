#pragma once

#include "bim/wall_size.h"
#include "engine/document/document.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace tamias {

// 视口里的"楼层带"（Y 轴向上）：**只来自文档的楼层表**（楼层设置里那张表）。
// 楼层不从几何推——画个东西就冒出一层、删掉又没了，那是错的。
// 它是**视图过滤**用的，不写回文档。
// 构件属于哪层看它自己的归属（族实体的 storey_id，见 entity_storey_id），
// 楼层带只给没归属的兜底，见 viewport_floor_allows。
struct ViewportFloor {
  std::string label;
  std::uint64_t storey_id = 0;  // 楼层表里的楼层 id
  float y_min = 0.f;
  float y_max = 0.f;
  float height = static_cast<float>(kDefaultWallHeight);  // 层高（米）
  bool mezzanine = false;
};

inline bool viewport_floor_contains(const ViewportFloor& floor, const Aabb& bounds) {
  if (!bounds.valid()) {
    return false;
  }
  return bounds.max.y > floor.y_min && bounds.min.y < floor.y_max;
}

// 按楼层过滤时这个节点算不算可见？hidden = 被隐藏的楼层下标（空 = 全可见）。
// 归属优先看 BIM 语义：有楼层归属的构件只看自己那层——在 1 楼画的**顶板**压在
// 2 楼标高上，它仍然是 1 楼的东西，不该掉进 2 楼。
// 没有楼层归属的（未归属构件 / 导入网格）退回几何楼层带：碰到任意一个可见的带就还
// 看得见（跨层构件不会因为少碰一层就消失）。
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

// 视口里的楼层 = **文档楼层表**，按标高排好。表里没有就是没有。
// 加层 / 删层只走「楼层设置」（UpdateStoreysCommand），这里只读。
inline std::vector<ViewportFloor> viewport_floors(const Document& doc) {
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
    floor.y_max = i + 1 < storeys.size() ? static_cast<float>(storeys[i + 1]->elevation)
                                         : floor.y_min + static_cast<float>(height);
    if (floor.y_max <= floor.y_min + 0.05f) {
      floor.y_max = floor.y_min + static_cast<float>(height);
    }
    floors.push_back(std::move(floor));
  }
  return floors;
}

}  // namespace tamias
