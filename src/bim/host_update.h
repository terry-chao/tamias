#pragma once

#include "engine/core/result.h"
#include "engine/math/math.h"

#include <cstdint>

namespace tamias {

class Document;

// 宿主（墙）变了：按关联找到从属开口 → 通知 → 重新造型 → 对齐 → 合法性检查。
Result<void> notify_entity_changed(Document& document, std::uint64_t entity_id);

// 把开口绑到墙上，写入 HostedOn 关联并立刻走一遍造型/对齐/检查。
Result<void> bind_opening_to_host(Document& document, std::uint64_t guest_id,
                                  std::uint64_t host_id, Vec3 world_point);

}  // namespace tamias
