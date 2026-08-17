#pragma once

#include "entity/entity.h"

namespace tamias {

// 梁：水平构件，矩形截面，由两端点和截面尺寸定义。
class BeamEntity final : public Entity {
 public:
  BeamEntity() { kind_ = EntityKind::Beam; }
  BeamEntity(Vec3 start, Vec3 end, double width, double depth);
};

}  // namespace tamias
