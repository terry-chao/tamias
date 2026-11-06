#pragma once

#include "engine/modeling/nodes/node_type.h"

namespace tamias {

// 各 builtin 注册文件的装配口（register_builtin_node_types 调用）。
// 注意与 node_type.h 里 register_builtin_node_types 同在 tamias 命名空间。
void register_input_nodes(NodeRegistry& registry);
void register_solid_nodes(NodeRegistry& registry);
void register_transform_nodes(NodeRegistry& registry);

}  // namespace tamias
