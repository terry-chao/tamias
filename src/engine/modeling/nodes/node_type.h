#pragma once

#include "engine/modeling/nodes/node_value.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tamias {

// solve 适配器在 node_eval.h 里定义；这里只存函数，避免头文件循环依赖。
struct SolveArgs;

// 参数控件形态（画在卡片中部；内联输入端口值也复用这套控件）。
enum class WidgetKind : std::uint8_t { Field = 0, Slider, Toggle, Choice };

struct PortSpec {
  std::string name;         // 显示名（英文源串）+ 稳定的文件标识
  PortType type = PortType::Empty;
  NodeValue default_value;  // 未连线时的内联初值
  bool optional = false;    // 缺输入不挡求值（solve 自己兜底）
};

struct NodeParamSpec {
  std::string key;  // 稳定标识（文件里按它匹配）
  NodeValue default_value;
  WidgetKind kind = WidgetKind::Field;
  double min = 0.0;
  double max = 0.0;  // kind == Slider 时有效
  std::vector<std::string> choices;  // kind == Choice 时的候选项（稳定标识）
};

// 节点类型 = 端口/参数的说明 + 一个 solve 函数。纯聚合、无宏、无反射；
// 注册表按字符串 id 索引。文件里存 type id，加载时对不上就是「丢失节点」。
struct NodeTypeInfo {
  std::string id;        // "solid.box" —— 文件里的稳定标识
  std::string name;      // 显示名（英文源串，中文关键词进 keywords）
  std::string category;  // "大类/小类"，两级；菜单、搜索、标题栏配色共用
  std::string keywords;  // 搜索关键词（含中文，比如 "盒体 方块"）
  std::vector<PortSpec> inputs;
  std::vector<PortSpec> outputs;
  std::vector<NodeParamSpec> params;
  std::function<void(SolveArgs&)> solve;  // 空函数 = 展示型节点（如 output）
};

// 类别的固定顺序（大类从数据到几何、从低维到高维；同大类内按字典序）。
// 菜单顺序、搜索默认排序、标题栏颜色都从这张表来。
[[nodiscard]] int node_category_order(std::string_view category);

// 内置节点类型的注册表。builtin() 是静态懒加载单例：注册一次、预排序，
// 之后只读（后台求值 P2 可以放心并发读）。
class NodeRegistry {
 public:
  struct Hit {
    const NodeTypeInfo* type = nullptr;
    int score = 0;  // 越大越相关
  };

  // 进程级内置注册表（首次调用会注册全部内置节点）。
  [[nodiscard]] static NodeRegistry& builtin();

  // 注册 / 覆盖（同 id 覆盖，给将来的 group 类型 / 插件用）。指针稳定。
  const NodeTypeInfo* register_type(NodeTypeInfo info);

  [[nodiscard]] const NodeTypeInfo* find(std::string_view id) const;

  // 按类别顺序排好的全量清单（右键菜单用）。
  [[nodiscard]] const std::vector<const NodeTypeInfo*>& ordered() const { return ordered_; }

  // 模糊搜索：大小写不敏感；精确 > 前缀 > 点号后段 > 包含 > 子序列；
  // 命中字段权重 name/id 3、category 2、keywords 2.7。limit == 0 表示不限。
  [[nodiscard]] std::vector<Hit> search(std::string_view query, std::size_t limit = 0) const;

  // 面向某个端口的搜索：先按兼容度分档（Type > Convert > None），
  // 再在档内按普通打分。从端口拖到空白画布弹搜索框时用。
  [[nodiscard]] std::vector<Hit> search_for_port(std::string_view query, PortType port,
                                                 bool as_input, std::size_t limit = 0) const;

 private:
  void rebuild_ordered();

  std::vector<std::unique_ptr<NodeTypeInfo>> owned_;
  std::unordered_map<std::string, NodeTypeInfo*> by_id_;
  std::vector<const NodeTypeInfo*> ordered_;
};

// 把全部内置节点类型注册进 registry（builtin() 内部也调它；测试可自建注册表复用）。
void register_builtin_node_types(NodeRegistry& registry);

}  // namespace tamias
