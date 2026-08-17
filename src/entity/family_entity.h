#pragma once

#include "entity/entity.h"

#include <string>
#include <utility>

namespace tamias {

// 族实体：所有 BIM 构件（墙、梁、柱、板、门、窗）的中间基类。
// 它仍是一个 Entity，但额外表达「族类型」和「族实例」语义；
// Box / Cylinder 等基础体不继承 FamilyEntity。
class FamilyEntity : public Entity {
 public:
  FamilyEntity() = default;
  ~FamilyEntity() override = default;

  [[nodiscard]] EntityKind family_category() const { return kind_; }
  [[nodiscard]] const std::string& family_type() const { return family_type_; }
  void set_family_type(std::string type) { family_type_ = std::move(type); }

  [[nodiscard]] bool is_family_entity() const final { return true; }

 protected:
  FamilyEntity(EntityKind category, std::string family_type)
      : family_type_(std::move(family_type)) {
    kind_ = category;
  }

 private:
  std::string family_type_ = "Generic Model";
};

}  // namespace tamias
