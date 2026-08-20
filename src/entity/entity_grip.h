#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <vector>

namespace tamias {

class Entity;

// One CAD-style grip on an entity: drag it to rebuild the geometry.
struct EntityGrip {
  std::uint64_t entity_id = 0;
  int index = 0;
  Vec3 world{};
};

[[nodiscard]] std::vector<EntityGrip> collect_entity_grips(const Entity& entity);
bool apply_entity_grip(Entity& entity, int index, Vec3 world);

// Fill Entity::grips from current feature tree + placement (local space).
void sync_entity_grips(Entity& entity);

}  // namespace tamias
