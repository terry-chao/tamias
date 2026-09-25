#pragma once

#include "entity/core/entity.h"

#include <cstdint>
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

  // 楼层归属：构件画在哪一层就属于哪一层（0 = 未归属）。
  // 这是归属的真源——写入口只有 Document::assign_storey / sync_entity_location，
  // 读入口统一走 entity_storey_id()（见 entity/core/entity_storey.h）；
  // Location 上的 storey_id 只是放置锚点，两者由那一处对齐。
  [[nodiscard]] std::uint64_t storey_id() const { return storey_id_; }
  void set_storey_id(std::uint64_t value) { storey_id_ = value; }

  [[nodiscard]] bool is_family_entity() const final { return true; }

 protected:
  FamilyEntity(EntityKind category, std::string family_type)
      : family_type_(std::move(family_type)) {
    kind_ = category;
  }

 private:
  std::string family_type_ = "Generic Model";
  std::uint64_t storey_id_ = 0;
};

}  // namespace tamias
