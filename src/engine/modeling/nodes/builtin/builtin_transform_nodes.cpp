#include "engine/modeling/nodes/builtin/builtin_nodes.h"

#include "engine/modeling/nodes/node_eval.h"

namespace tamias {

void register_transform_nodes(NodeRegistry& registry) {
  NodeTypeInfo move;
  move.id = "xform.move";
  move.name = "Move";
  move.category = "变换/移动";
  move.keywords = "移动 平移 move translate offset";
  move.inputs.push_back(PortSpec{"Body", PortType::Body});
  move.inputs.push_back(PortSpec{"Offset", PortType::Vector, NodeValue::vector({0, 0, 0})});
  move.outputs.push_back(PortSpec{"Body", PortType::Body});
  move.solve = [](SolveArgs& a) {
    ModelKernel* k = a.kernel();
    if (k == nullptr) {
      a.error("no modeling kernel");
      return;
    }
    const BodyRef input = a.in_body(0);
    if (input == nullptr) {
      a.error("Move: missing body input");
      return;
    }
    const Vec3 offset = a.in_vector(1, {0, 0, 0});
    auto body = k->transform(*input, offset);
    if (!body) {
      a.error("transform: " + body.error());
      return;
    }
    a.out(0, NodeValue::body(std::move(*body)));
  };
  registry.register_type(std::move(move));
}

}  // namespace tamias
