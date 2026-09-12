#pragma once

#include "bim/wall_size.h"
#include "engine/document/document.h"
#include "entity/entity.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace tamias {

// Viewport display band inferred from geometry elevation (Y-up).
// Not a BIM storey — just a view filter until BimModel storeys exist.
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
