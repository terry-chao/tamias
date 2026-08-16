#pragma once

#include "entity/entity.h"

namespace tamias {

// 墙实体：墙厚 × 墙高 × 两点间长度，放置在两点之间的地面上。
class WallEntity final : public Entity {
 public:
  WallEntity() { kind_ = EntityKind::Wall; }
  // drag 操作：两点 → 墙（纯操作，无鼠标）。
  static WallEntity drag(Vec3 start, Vec3 end, double thickness, double height);
};

}  // namespace tamias
