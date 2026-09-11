#pragma once

#include "entity/family_entity.h"

namespace tamias {

// 开洞实体：门 / 窗共同继承的中间基类。
// 它仍然是 FamilyEntity，但额外表达「墙上的开洞」语义；
// 离地高度（sill）是开洞自身的属性，而不是放置时从点击点反算。
class OpeningEntity : public FamilyEntity {
 public:
  OpeningEntity(EntityKind category, std::string family_type, double default_sill)
      : FamilyEntity(category, std::move(family_type)), default_sill_(default_sill) {}
  ~OpeningEntity() override = default;

  [[nodiscard]] double sill_height() const {
    for (const Feature& feature : model.features()) {
      if (feature.kind == FeatureKind::RectProfile) {
        return model.param(feature.id, "sill", default_sill_);
      }
    }
    return default_sill_;
  }

  void set_sill_height(double sill) {
    for (Feature& feature : model.features()) {
      if (feature.kind == FeatureKind::RectProfile) {
        model.set_param(feature.id, "sill", sill);
        return;
      }
    }
  }

  [[nodiscard]] double default_sill_height() const { return default_sill_; }

 private:
  double default_sill_ = 0.0;
};

[[nodiscard]] inline bool is_opening_entity(const Entity& entity) {
  return dynamic_cast<const OpeningEntity*>(&entity) != nullptr;
}

[[nodiscard]] inline double opening_sill_height(const Entity& entity) {
  const auto* opening = dynamic_cast<const OpeningEntity*>(&entity);
  return opening != nullptr ? opening->sill_height() : 0.0;
}

}  // namespace tamias
