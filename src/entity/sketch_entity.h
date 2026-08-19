#pragma once

#include "entity/entity.h"

namespace tamias {

// 草图实体：直线、折线、圆、圆弧、贝塞尔、矩形等。直接继承 Entity，不是族实体。
class SketchEntity : public Entity {
 public:
  SketchEntity() = default;
  ~SketchEntity() override = default;

  [[nodiscard]] bool is_sketch_entity() const final { return true; }

 protected:
  explicit SketchEntity(EntityKind kind) { kind_ = kind; }
};

}  // namespace tamias
