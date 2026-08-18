#pragma once

#include "bim/relation.h"

#include <cstdint>
#include <vector>

namespace tamias {

// BIM 文档侧面：关联关系表。和 Scene、实体表并列，随 .tdoc 一起存。
// 楼层 / 轴网以后也挂在这里；现在先做宿主关联。
class BimModel {
 public:
  Relation& add(Relation relation);
  Relation& insert(Relation relation);
  void remove(std::uint64_t id);
  void remove_involving(std::uint64_t entity_id);
  void clear();

  [[nodiscard]] Relation* find(std::uint64_t id);
  [[nodiscard]] const Relation* find(std::uint64_t id) const;
  [[nodiscard]] Relation* host_of(std::uint64_t guest_id);
  [[nodiscard]] const Relation* host_of(std::uint64_t guest_id) const;

  // to == host_id 的关联（墙上的窗/门）。
  [[nodiscard]] std::vector<Relation*> dependents(std::uint64_t host_id);
  [[nodiscard]] std::vector<const Relation*> dependents(std::uint64_t host_id) const;

  [[nodiscard]] const std::vector<Relation>& relations() const { return relations_; }
  [[nodiscard]] std::vector<Relation>& relations() { return relations_; }
  [[nodiscard]] std::uint64_t next_id() const { return next_id_; }
  void set_next_id(std::uint64_t id);

 private:
  std::vector<Relation> relations_;
  std::uint64_t next_id_ = 1;
};

}  // namespace tamias
