#pragma once

#include "bim/wall_size.h"
#include "engine/document/document.h"
#include "entity/entity.h"

#include <algorithm>
#include <string>
#include <vector>

namespace tamias {

// Viewport display band inferred from geometry elevation (Y-up).
// Not a BIM storey — just a view filter until BimModel storeys exist.
struct ViewportFloor {
  std::string label;
  float y_min = 0.f;
  float y_max = 0.f;
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
      return true;
    default:
      return false;
  }
}

inline std::vector<ViewportFloor> infer_viewport_floors(const Document& doc) {
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
