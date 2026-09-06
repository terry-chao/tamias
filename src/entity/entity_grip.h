#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <vector>

namespace tamias {

class Entity;

// One CAD-style grip on an entity: drag previews; drop rebuilds geometry.
struct EntityGrip {
  std::uint64_t entity_id = 0;
  int index = 0;
  Vec3 world{};
};

[[nodiscard]] std::vector<EntityGrip> collect_entity_grips(const Entity& entity);
bool apply_entity_grip(Entity& entity, int index, Vec3 world);

// Cheap overlay polyline for a live grip drag (no OCCT / createGeom).
[[nodiscard]] std::vector<Vec3> grip_preview_polyline(const Entity& entity);

// Fill Entity::grips from current feature tree + placement (local space).
void sync_entity_grips(Entity& entity);

}  // namespace tamias
