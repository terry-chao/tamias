#pragma once

#include "entity/core/entity.h"
#include "entity/family/family_entity.h"

#include <cstdint>

namespace tamias {

// 构件属于哪一层（归属的唯一读入口）。
//
// 族实体（墙 / 梁 / 柱 / 板 / 基础 / 幕墙 / 门窗）自带 `FamilyEntity::storey_id()`：
// 归属在**创建那一刻**就定下来，之后不随"当前楼层"漂移（见 Document::assign_storey）。
// 没有族语义的实体（Box / Cylinder / 草图）：退回 Location 上的楼层；也没有 → 0（未归属）。
[[nodiscard]] inline std::uint64_t entity_storey_id(const Entity& entity) {
  // is_family_entity() 只在 FamilyEntity 上为 true（final），比 dynamic_cast 便宜，
  // 这条又走在每帧的可见性过滤里。
  if (entity.is_family_entity()) {
    return static_cast<const FamilyEntity&>(entity).storey_id();
  }
  return entity.location != nullptr ? entity.location->storey_id() : 0;
}

}  // namespace tamias
