#pragma once

#include "bim/host_placement.h"
#include "bim/opening_size.h"
#include "bim/wall_size.h"
#include "engine/math/math.h"
#include "entity/entity.h"

#include <vector>

namespace tamias {

[[nodiscard]] WallSize wall_size(const Entity& wall);
[[nodiscard]] OpeningSize opening_size(const Entity& opening);
void set_opening_thickness(Entity& opening, double thickness);

[[nodiscard]] bool is_wall_host(const Entity& host);
[[nodiscard]] bool can_host_opening(const Entity& host, const Entity& guest);

// 世界点 → 墙上的参数化位置（沿长 / 离地高度 / 墙厚偏移）。
// sill_height 来自门窗实体属性，不再由点击高度反算。
[[nodiscard]] HostPlacement placement_from_world(const Entity& wall, const OpeningSize& opening,
                                                 Vec3 world_point, double sill_height);
[[nodiscard]] HostPlacement placement_from_world(const Entity& wall, const Entity& guest,
                                                 Vec3 world_point);

// 布置门窗时画在墙面上的开口线框（两侧矩形）。
[[nodiscard]] std::vector<Vec3> opening_preview_polyline(const Entity& wall,
                                                         const OpeningSize& opening,
                                                         Vec3 world_point, double sill_height);

// 对齐：把开口夹进墙的可用范围。
void align_placement(HostPlacement& placement, const WallSize& wall, const OpeningSize& opening);

// 合法性：开口在墙长、墙高上是否仍完全落在墙内。
[[nodiscard]] bool placement_is_valid(const HostPlacement& placement, const WallSize& wall,
                                      const OpeningSize& opening);

// 从宿主墙 + 参数化位置算出开口的 local_transform。
[[nodiscard]] Mat4 hosted_transform(const Entity& wall, const HostPlacement& placement);

}  // namespace tamias
