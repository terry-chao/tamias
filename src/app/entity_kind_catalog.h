#pragma once

#include "entity/entity.h"

#include <QString>
#include <vector>

namespace tamias {

// 构件类型的展示元数据。可见性面板与其它按类别列构件的界面共用这一份，
// 避免同一个 EntityKind 在多处各写一套名称/图标（以前视口菜单和属性面板就是两份）。
struct EntityKindEntry {
  EntityKind kind = EntityKind::Wall;
  const char* icon = "";  // Qt 资源路径，如 ":/icons/wall.svg"
};

// 按界面显示顺序（建筑 → 结构 → 草图/基础体）排好的全部构件类型。
[[nodiscard]] const std::vector<EntityKindEntry>& entity_kind_catalog();

// 该类构件的界面名称，如 "Walls"（中文 "墙"）。
[[nodiscard]] QString entity_kind_label(EntityKind kind);

// 专业分组名，如 "Architectural"（中文 "建筑"）。
[[nodiscard]] QString discipline_label(Discipline discipline);

}  // namespace tamias
