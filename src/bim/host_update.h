#pragma once

#include "bim/relation.h"
#include "engine/core/result.h"
#include "engine/math/math.h"
#include "engine/modeling/feature.h"
#include "entity/entity.h"

#include <cstdint>
#include <vector>

namespace tamias {

class Document;

// 宿主（墙）变了：按关联找到从属开口 → 通知 → 重新造型 → 对齐 → 合法性检查。
Result<void> notify_entity_changed(Document& document, std::uint64_t entity_id);

// 把开口绑到墙上，写入 HostedOn 关联并立刻走一遍造型/对齐/检查。
Result<void> bind_opening_to_host(Document& document, std::uint64_t guest_id,
                                  std::uint64_t host_id, Vec3 world_point);

// 在已有输出特征 current 上追加开洞切减，并把 current 推进到新的布尔结果。
// 墙的渲染模型（墙-墙倒角 + 开口切减，见 wall_join.h）用这一段。
void append_hosted_opening_cuts(FeatureModel& model, std::uint64_t& current, const Entity& host,
                                const std::vector<const Relation*>& openings,
                                const Document& document);

// 按墙上的开口把洞切进宿主网格（不改宿主特征树）。无开口则恢复实心墙。
// 与墙-墙倒角是同一条造型路径，最终都走 wall_join.h 的 remesh_wall。
Result<void> remesh_host_openings(Document& document, std::uint64_t host_id);

}  // namespace tamias
