#include "engine/modeling/nodes/node_io.h"

#include "engine/modeling/nodes/node_type.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace tamias {

namespace {

Result<void> write_value(BinaryWriter& w, const NodeValue& v) {
  if (auto r = w.write_u8(static_cast<std::uint8_t>(v.type())); !r) {
    return r;
  }
  switch (v.type()) {
    case PortType::Empty:
      return {};
    case PortType::Number:
      return w.write_f64(v.as_number());
    case PortType::Integer:
      return w.write_u64(static_cast<std::uint64_t>(v.as_integer()));
    case PortType::Bool:
      return w.write_bool(v.as_bool());
    case PortType::Text:
      return w.write_string(v.as_text());
    case PortType::Vector: {
      const Vec3 u = v.as_vector();
      if (auto r = w.write_f64(u.x); !r) {
        return r;
      }
      if (auto r = w.write_f64(u.y); !r) {
        return r;
      }
      return w.write_f64(u.z);
    }
    case PortType::Body:
      return Err("node value of Body is an evaluation result and is not serialized");
  }
  return Err("unknown node value type");
}

Result<NodeValue> read_value(BinaryReader& r) {
  auto tag = r.read_u8();
  if (!tag) {
    return Err(tag.error());
  }
  switch (static_cast<PortType>(*tag)) {
    case PortType::Empty:
      return NodeValue::empty();
    case PortType::Number: {
      auto v = r.read_f64();
      if (!v) {
        return Err(v.error());
      }
      return NodeValue::number(*v);
    }
    case PortType::Integer: {
      auto v = r.read_u64();
      if (!v) {
        return Err(v.error());
      }
      return NodeValue::integer(static_cast<std::int64_t>(*v));
    }
    case PortType::Bool: {
      auto v = r.read_bool();
      if (!v) {
        return Err(v.error());
      }
      return NodeValue::boolean(*v);
    }
    case PortType::Text: {
      auto v = r.read_string();
      if (!v) {
        return Err(v.error());
      }
      return NodeValue::text(std::move(*v));
    }
    case PortType::Vector: {
      auto x = r.read_f64();
      if (!x) {
        return Err(x.error());
      }
      auto y = r.read_f64();
      if (!y) {
        return Err(y.error());
      }
      auto z = r.read_f64();
      if (!z) {
        return Err(z.error());
      }
      return NodeValue::vector(Vec3{static_cast<float>(*x), static_cast<float>(*y),
                                    static_cast<float>(*z)});
    }
    case PortType::Body:
      return Err("serialized node value claims to be a Body");
  }
  return Err("unknown serialized node value type");
}

}  // namespace

Result<std::vector<std::uint8_t>> write_node_graph(const Graph& graph) {
  BinaryWriter w;
  if (auto r = w.write_u32(kNodeGraphFormatVersion); !r) {
    return Err(r.error());
  }

  if (auto r = w.write_u64(graph.next_id()); !r) {
    return Err(r.error());
  }

  if (auto r = w.write_u64(static_cast<std::uint64_t>(graph.nodes().size())); !r) {
    return Err(r.error());
  }
  for (const auto& node : graph.nodes()) {
    if (auto r = w.write_u64(node->id); !r) {
      return Err(r.error());
    }
    if (auto r = w.write_string(node->type_id); !r) {
      return Err(r.error());
    }
    if (auto r = w.write_f64(node->x); !r) {
      return Err(r.error());
    }
    if (auto r = w.write_f64(node->y); !r) {
      return Err(r.error());
    }
    if (auto r = w.write_bool(node->preview); !r) {
      return Err(r.error());
    }
    // 内联输入值：端口名 + 值（按名匹配，抗端口演化）。丢失节点没有名字，
    // 存空名、按位保留（值不能丢，等类型注册回来还能用）。
    if (auto r = w.write_u64(static_cast<std::uint64_t>(node->input_values.size())); !r) {
      return Err(r.error());
    }
    const std::size_t input_count =
        node->type != nullptr ? std::min(node->input_values.size(), node->type->inputs.size())
                              : node->input_values.size();
    for (std::size_t p = 0; p < input_count; ++p) {
      const std::string& name =
          node->type != nullptr ? node->type->inputs[p].name : std::string();
      if (auto r = w.write_string(name); !r) {
        return Err(r.error());
      }
      if (auto r = write_value(w, node->input_values[p]); !r) {
        return Err(r.error());
      }
    }
    // 参数值：key + 值（丢失节点同样按位保留）。
    if (auto r = w.write_u64(static_cast<std::uint64_t>(node->params.size())); !r) {
      return Err(r.error());
    }
    const std::size_t param_count =
        node->type != nullptr ? std::min(node->params.size(), node->type->params.size())
                              : node->params.size();
    for (std::size_t p = 0; p < param_count; ++p) {
      const std::string& key =
          node->type != nullptr ? node->type->params[p].key : std::string();
      if (auto r = w.write_string(key); !r) {
        return Err(r.error());
      }
      if (auto r = write_value(w, node->params[p]); !r) {
        return Err(r.error());
      }
    }
  }

  if (auto r = w.write_u64(static_cast<std::uint64_t>(graph.links().size())); !r) {
    return Err(r.error());
  }
  for (const Link& link : graph.links()) {
    if (auto r = w.write_u64(link.from_node); !r) {
      return Err(r.error());
    }
    if (auto r = w.write_u16(link.from_port); !r) {
      return Err(r.error());
    }
    if (auto r = w.write_u64(link.to_node); !r) {
      return Err(r.error());
    }
    if (auto r = w.write_u16(link.to_port); !r) {
      return Err(r.error());
    }
  }
  return w.release();
}

Result<void> read_node_graph(Graph& target, std::span<const std::uint8_t> data) {
  BinaryReader r(data);

  auto version = r.read_u32();
  if (!version) {
    return Err(version.error());
  }
  if (*version > kNodeGraphFormatVersion) {
    return Err("node graph format version " + std::to_string(*version) +
               " is newer than supported " + std::to_string(kNodeGraphFormatVersion));
  }

  auto next_id = r.read_u64();
  if (!next_id) {
    return Err(next_id.error());
  }

  // 读进一个临时图，全部成功后再换给 target（半截档不要往外漏）。
  Graph fresh;
  const NodeRegistry& registry = NodeRegistry::builtin();

  auto node_count = r.read_u64();
  if (!node_count) {
    return Err(node_count.error());
  }
  for (std::uint64_t i = 0; i < *node_count; ++i) {
    auto id = r.read_u64();
    if (!id) {
      return Err(id.error());
    }
    auto type_id = r.read_string();
    if (!type_id) {
      return Err(type_id.error());
    }
    auto x = r.read_f64();
    if (!x) {
      return Err(x.error());
    }
    auto y = r.read_f64();
    if (!y) {
      return Err(y.error());
    }
    auto preview = r.read_bool();
    if (!preview) {
      return Err(preview.error());
    }

    auto node = std::make_unique<Graph::Node>();
    node->id = *id;
    node->type_id = std::move(*type_id);
    node->type = registry.find(node->type_id);
    node->x = *x;
    node->y = *y;
    node->preview = *preview;
    if (node->type != nullptr) {
      node->input_values.reserve(node->type->inputs.size());
      for (const PortSpec& spec : node->type->inputs) {
        node->input_values.push_back(spec.default_value);
      }
      node->params.reserve(node->type->params.size());
      for (const NodeParamSpec& spec : node->type->params) {
        node->params.push_back(spec.default_value);
      }
    }

    auto input_count = r.read_u64();
    if (!input_count) {
      return Err(input_count.error());
    }
    for (std::uint64_t p = 0; p < *input_count; ++p) {
      auto name = r.read_string();
      if (!name) {
        return Err(name.error());
      }
      auto value = read_value(r);
      if (!value) {
        return Err(value.error());
      }
      // 按名匹配写回；名字对不上的（类型演化后删掉的端口）直接丢弃。
      // 丢失节点没有规格可对，按位保留。
      if (node->type != nullptr) {
        for (std::size_t q = 0; q < node->type->inputs.size(); ++q) {
          if (node->type->inputs[q].name == *name) {
            node->input_values[q] = std::move(*value);
            break;
          }
        }
      } else {
        node->input_values.push_back(std::move(*value));
      }
    }

    auto param_count = r.read_u64();
    if (!param_count) {
      return Err(param_count.error());
    }
    for (std::uint64_t p = 0; p < *param_count; ++p) {
      auto key = r.read_string();
      if (!key) {
        return Err(key.error());
      }
      auto value = read_value(r);
      if (!value) {
        return Err(value.error());
      }
      if (node->type != nullptr) {
        for (std::size_t q = 0; q < node->type->params.size(); ++q) {
          if (node->type->params[q].key == *key) {
            node->params[q] = std::move(*value);
            break;
          }
        }
      } else {
        node->params.push_back(std::move(*value));
      }
    }

    fresh.insert_node(std::move(node));
  }

  auto link_count = r.read_u64();
  if (!link_count) {
    return Err(link_count.error());
  }
  for (std::uint64_t i = 0; i < *link_count; ++i) {
    auto from_node = r.read_u64();
    if (!from_node) {
      return Err(from_node.error());
    }
    auto from_port = r.read_u16();
    if (!from_port) {
      return Err(from_port.error());
    }
    auto to_node = r.read_u64();
    if (!to_node) {
      return Err(to_node.error());
    }
    auto to_port = r.read_u16();
    if (!to_port) {
      return Err(to_port.error());
    }
    // 连不上的（类型演化后端口没了 / 类型不兼容了）跳过这条线，
    // 不让一根坏线毁掉整个文档；能连的照常走 connect 的全部检查。
    (void)fresh.connect(*from_node, *from_port, *to_node, *to_port);
  }

  fresh.set_next_id(*next_id);
  fresh.mark_all_dirty();
  target = std::move(fresh);
  return {};
}

}  // namespace tamias
