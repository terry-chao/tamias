#pragma once

#include "bim/grid.h"
#include "bim/relation.h"
#include "bim/storey.h"

#include <cstdint>
#include <vector>

namespace tamias {

// BIM 文档侧面：关联关系表 + 楼层表 + 轴网。和 Scene、实体表并列，随 .tdoc 一起存。
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

  Storey& insert_storey(Storey storey);
  void remove_storey(std::uint64_t id);
  [[nodiscard]] Storey* find_storey(std::uint64_t id);
  [[nodiscard]] const Storey* find_storey(std::uint64_t id) const;
  [[nodiscard]] const std::vector<Storey>& storeys() const { return storeys_; }
  [[nodiscard]] std::vector<Storey>& storeys() { return storeys_; }
  [[nodiscard]] std::uint64_t active_storey_id() const { return active_storey_id_; }
  void set_active_storey_id(std::uint64_t id);
  [[nodiscard]] double storey_elevation(std::uint64_t id) const;

  // 轴网：定位参考，不是构件；不进实体表，也不进渲染合批。
  [[nodiscard]] Grid& grid() { return grid_; }
  [[nodiscard]] const Grid& grid() const { return grid_; }

 private:
  std::vector<Relation> relations_;
  std::vector<Storey> storeys_;
  Grid grid_;
  std::uint64_t active_storey_id_ = 0;
  std::uint64_t next_id_ = 1;
};

}  // namespace tamias
