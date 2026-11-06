#include "engine/modeling/nodes/builtin/builtin_nodes.h"

namespace tamias {

void register_builtin_node_types(NodeRegistry& registry) {
  register_input_nodes(registry);
  register_solid_nodes(registry);
  register_transform_nodes(registry);
}

}  // namespace tamias
