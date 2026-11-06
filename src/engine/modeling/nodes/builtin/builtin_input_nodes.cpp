#include "engine/modeling/nodes/builtin/builtin_nodes.h"

#include "engine/modeling/nodes/node_eval.h"

namespace tamias {

// input.number：一个滑条数值源。范围由 min/max 参数控制（滑条两端）。
void register_input_nodes(NodeRegistry& registry) {
  NodeTypeInfo number;
  number.id = "input.number";
  number.name = "Number";
  number.category = "输入/数值";
  number.keywords = "数值 数字 滑条 slider number";
  number.outputs.push_back(PortSpec{"Value", PortType::Number, NodeValue::number(1.0)});
  number.params.push_back(NodeParamSpec{
      "value", NodeValue::number(1.0), WidgetKind::Slider, 0.0, 10.0, {}});
  number.params.push_back(NodeParamSpec{"min", NodeValue::number(0.0)});
  number.params.push_back(NodeParamSpec{"max", NodeValue::number(10.0)});
  number.solve = [](SolveArgs& a) {
    a.out(0, NodeValue::number(a.param_number("value", 1.0)));
  };
  registry.register_type(std::move(number));
}

}  // namespace tamias
