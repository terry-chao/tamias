#pragma once

#include "bim/wall_size.h"
#include "engine/core/result.h"
#include "engine/math/math.h"
#include "engine/modeling/feature.h"
#include "entity/entity.h"

#include <cstdint>
#include <vector>

namespace tamias {

class Document;

// 墙-墙交接（倒角 / 斜接）。
//
// 两面墙在端点相接时，现实里是一个斜接角：两墙共用一个竖直斜接面，各自延长
// 到该面再裁掉。之前是两段长方体硬拼——外角缺一个方块，而且两墙的侧面正好
// 共面，渲染上会闪烁。
//
// 一个 WallJoint 描述「本墙某一端按哪个斜接面裁」：面在墙局部 XZ 平面（X=厚、
// Z=长）里用「过点 + 法线」表示，法线指向被裁掉的一侧；extension 是该端要先
// 沿墙轴延长的长度（不延长就补不满外角）。
struct WallJoint {
  std::uint64_t neighbor_id = 0;  // 交接的邻墙实体 id
  bool at_start = false;          // true = 本墙起点，false = 终点
  Vec2 normal_local{};            // 斜接面法线（墙局部 XZ，单位向量）
  Vec2 point_local{};             // 斜接面过点（墙局部 XZ）
  double extension = 0.0;         // 该端需延长的长度（含咬入余量）
};

// 本墙两端与同标高邻墙（墙 / 结构墙）的交接。无交接返回空。
[[nodiscard]] std::vector<WallJoint> find_wall_junctions(const Entity& wall,
                                                         const Document& document);

// 墙的平面轮廓（局部 XZ，起点在 -Z、终点在 +Z）：两端按交接延长后用斜接面裁。
// joints 为空时就是普通矩形四角。
[[nodiscard]] std::vector<Vec3> wall_junction_footprint(const WallSize& size,
                                                        const std::vector<WallJoint>& joints);

// 带倒角的墙特征树：基础轮廓换成裁好的 PolygonProfile，其余特征（空腔内箱、
// 开口切减）按拓扑序保留。joints 为空或形状不匹配时返回原模型。
[[nodiscard]] FeatureModel wall_junction_model(const Entity& wall,
                                               const std::vector<WallJoint>& joints);

// 造型/网格用的墙模型：墙-墙倒角 + 宿主开口切减。非墙实体原样返回。
[[nodiscard]] FeatureModel wall_render_model(const Entity& wall, const Document& document);

// 与本墙交接的墙 id（含自己）。交接变了这些墙都要重新造型。
[[nodiscard]] std::vector<std::uint64_t> wall_neighborhood(const Document& document,
                                                           std::uint64_t wall_id);

// 重新造型一面墙（倒角 + 开口切减），写回网格资产。
Result<void> remesh_wall(Document& document, std::uint64_t wall_id);

// 重新造型一面墙及其邻墙。
Result<void> remesh_wall_neighborhood(Document& document, std::uint64_t wall_id);

// 只重新造型邻墙（本墙由调用方自己重建时用，避免多算一次）。
Result<void> remesh_wall_neighbors(Document& document, std::uint64_t wall_id);

}  // namespace tamias
