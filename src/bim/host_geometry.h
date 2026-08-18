#pragma once

#include "bim/host_placement.h"
#include "bim/opening_size.h"
#include "bim/wall_size.h"
#include "engine/math/math.h"
#include "entity/entity.h"

namespace tamias {

[[nodiscard]] WallSize wall_size(const Entity& wall);
[[nodiscard]] OpeningSize opening_size(const Entity& opening);
void set_opening_thickness(Entity& opening, double thickness);

[[nodiscard]] bool can_host_opening(const Entity& host, const Entity& guest);

// 世界点 → 墙上的参数化位置（沿长 / 窗台高 / 墙厚偏移）。门的 sill 固定为 0。
[[nodiscard]] HostPlacement placement_from_world(const Entity& wall, const Entity& guest,
                                                 Vec3 world_point);

// 对齐：把开口夹进墙的可用范围。
void align_placement(HostPlacement& placement, const WallSize& wall, const OpeningSize& opening);

// 合法性：开口在墙长、墙高上是否仍完全落在墙内。
[[nodiscard]] bool placement_is_valid(const HostPlacement& placement, const WallSize& wall,
                                      const OpeningSize& opening);

// 从宿主墙 + 参数化位置算出开口的 local_transform。
[[nodiscard]] Mat4 hosted_transform(const Entity& wall, const HostPlacement& placement);

}  // namespace tamias
