#pragma once

#include "bim/host_placement.h"
#include "bim/relation_kind.h"

#include <cstdint>

namespace tamias {

// 一条关联：from 依赖 to。例如窗 (from) HostedOn 墙 (to)。
struct Relation {
  std::uint64_t id = 0;
  RelationKind kind = RelationKind::HostedOn;
  std::uint64_t from = 0;  // 从属（窗、门）
  std::uint64_t to = 0;    // 宿主（墙）
  HostPlacement placement{};
  bool valid = true;  // 对齐后的合法性（开口是否还落在墙内）
};

}  // namespace tamias
