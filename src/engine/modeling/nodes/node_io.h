#pragma once

#include "engine/base/result.h"
#include "engine/io/binary_archive.h"
#include "engine/modeling/nodes/node_graph.h"

#include <cstdint>
#include <vector>

namespace tamias {

// 节点图的序列化（.tdoc 的 Node chunk 与将来的 .tnode 独立文件共用）。
//
// 约定：
//  - 只存结构与标量（类型 id、位置、内联值、参数值、连线），**不存几何**
//    （Body 是求值结果，加载后 mark_all_dirty 全量重算）；
//  - 端口 / 参数按**名字**匹配：节点类型加了新端口旧档也读得回
//    （多余条目丢弃，缺失条目取默认）；
//  - 未知节点类型 → 丢失节点（保数据、错误态），别让旧档读进来就崩；
//  - 带版本号且加载时校验（MiniCAD 写了 version 不检查的教训）。
inline constexpr std::uint32_t kNodeGraphFormatVersion = 1;

[[nodiscard]] Result<std::vector<std::uint8_t>> write_node_graph(const Graph& graph);
// 读进一个空图（会先清空 target）。加载后图是全脏的，等下一次求值。
[[nodiscard]] Result<void> read_node_graph(Graph& target,
                                           std::span<const std::uint8_t> data);

}  // namespace tamias
