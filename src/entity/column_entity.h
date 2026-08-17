#pragma once

#include "entity/entity.h"

namespace tamias {

// 柱：竖向构件，矩形截面，底部中心由 position 指定。
class ColumnEntity final : public Entity {
 public:
  ColumnEntity() { kind_ = EntityKind::Column; }
  explicit ColumnEntity(Vec3 position, double width = 0.4, double depth = 0.4,
                        double height = 3.0);
};

}  // namespace tamias
