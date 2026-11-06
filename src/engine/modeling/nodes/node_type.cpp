#include "engine/modeling/nodes/node_type.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace tamias {

namespace {

[[nodiscard]] std::string to_lower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

// 单字段匹配强度：精确 100 / 前缀 80 / 点号后段前缀或精确 75 / 包含 60 / 子序列 20。
[[nodiscard]] int match_score(const std::string& haystack_lower, const std::string& needle_lower) {
  if (needle_lower.empty()) {
    return 1;  // 空查询 = 全部命中（等分，交给类别顺序排）。
  }
  if (haystack_lower == needle_lower) {
    return 100;
  }
  if (haystack_lower.starts_with(needle_lower)) {
    return 80;
  }
  // id 的点号后段（"solid.box" 用 "box" 搜）：按前缀或精确给分。
  const std::size_t dot = haystack_lower.find_last_of('.');
  if (dot != std::string::npos) {
    const std::string_view suffix = std::string_view(haystack_lower).substr(dot + 1);
    if (suffix == needle_lower) {
      return 75;
    }
    if (suffix.starts_with(needle_lower)) {
      return 70;
    }
  }
  if (haystack_lower.find(needle_lower) != std::string::npos) {
    return 60;
  }
  // 子序列：查询词的字符按顺序散布在字段里。
  std::size_t at = 0;
  bool is_subsequence = true;
  for (char c : needle_lower) {
    const std::size_t found = haystack_lower.find(c, at);
    if (found == std::string::npos) {
      is_subsequence = false;
      break;
    }
    at = found + 1;
  }
  return is_subsequence ? 20 : 0;
}

[[nodiscard]] int score_type(const NodeTypeInfo& type, const std::string& query_lower) {
  const int name_id = std::max(match_score(to_lower(type.name), query_lower),
                               match_score(to_lower(type.id), query_lower));
  const int category = match_score(to_lower(type.category), query_lower);
  const int keywords = match_score(to_lower(type.keywords), query_lower);
  return std::max({name_id * 3, category * 2, keywords * 3});  // keywords 与名字同级
}

}  // namespace

const char* to_string(PortType type) {
  switch (type) {
    case PortType::Empty:
      return "Empty";
    case PortType::Number:
      return "Number";
    case PortType::Integer:
      return "Integer";
    case PortType::Bool:
      return "Bool";
    case PortType::Text:
      return "Text";
    case PortType::Vector:
      return "Vector";
    case PortType::Body:
      return "Body";
  }
  return "Empty";
}

PortMatch port_match(PortType from, PortType to) {
  if (from == to) {
    return from == PortType::Empty ? PortMatch::None : PortMatch::Type;
  }
  switch (from) {
    case PortType::Number:
      if (to == PortType::Integer || to == PortType::Bool) {
        return PortMatch::Convert;
      }
      break;
    case PortType::Integer:
      if (to == PortType::Number || to == PortType::Bool) {
        return PortMatch::Convert;
      }
      break;
    case PortType::Bool:
      if (to == PortType::Number || to == PortType::Integer) {
        return PortMatch::Convert;
      }
      break;
    default:
      break;
  }
  return PortMatch::None;
}

int node_category_order(std::string_view category) {
  // 大类固定顺序：输入 → 实体 → 变换 → 输出（从数据到几何、从低维到高维）。
  static const std::string_view kOrder[] = {
      "输入", "实体", "变换", "输出",
  };
  const std::size_t dot = category.find('/');
  const std::string_view major = dot == std::string_view::npos ? category : category.substr(0, dot);
  for (std::size_t i = 0; i < std::size(kOrder); ++i) {
    if (major == kOrder[i]) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(std::size(kOrder));  // 未知大类排最后
}

NodeRegistry& NodeRegistry::builtin() {
  static NodeRegistry registry;
  static std::once_flag once;
  std::call_once(once, [] { register_builtin_node_types(registry); });
  return registry;
}

const NodeTypeInfo* NodeRegistry::register_type(NodeTypeInfo info) {
  std::string id = info.id;
  const auto it = by_id_.find(id);
  if (it != by_id_.end()) {
    *it->second = std::move(info);  // 覆盖（group / 插件 upsert），指针不动。
    rebuild_ordered();
    return it->second;
  }
  auto owned = std::make_unique<NodeTypeInfo>(std::move(info));
  NodeTypeInfo* raw = owned.get();
  by_id_[std::move(id)] = raw;
  owned_.push_back(std::move(owned));
  rebuild_ordered();
  return raw;
}

const NodeTypeInfo* NodeRegistry::find(std::string_view id) const {
  const auto it = by_id_.find(std::string(id));
  return it == by_id_.end() ? nullptr : it->second;
}

void NodeRegistry::rebuild_ordered() {
  ordered_.clear();
  ordered_.reserve(owned_.size());
  for (const auto& type : owned_) {
    ordered_.push_back(type.get());
  }
  std::stable_sort(ordered_.begin(), ordered_.end(),
                   [](const NodeTypeInfo* a, const NodeTypeInfo* b) {
                     const int ca = node_category_order(a->category);
                     const int cb = node_category_order(b->category);
                     if (ca != cb) {
                       return ca < cb;
                     }
                     return a->category < b->category;  // 同大类内按小类字典序
                   });
}

std::vector<NodeRegistry::Hit> NodeRegistry::search(std::string_view query,
                                                    std::size_t limit) const {
  const std::string query_lower = to_lower(query);
  std::vector<Hit> hits;
  hits.reserve(ordered_.size());
  for (const NodeTypeInfo* type : ordered_) {
    const int score = score_type(*type, query_lower);
    if (score > 0) {
      hits.push_back(Hit{type, score});
    }
  }
  std::stable_sort(hits.begin(), hits.end(),
                   [](const Hit& a, const Hit& b) { return a.score > b.score; });
  if (limit != 0 && hits.size() > limit) {
    hits.resize(limit);
  }
  return hits;
}

std::vector<NodeRegistry::Hit> NodeRegistry::search_for_port(std::string_view query,
                                                             PortType port, bool as_input,
                                                             std::size_t limit) const {
  // 先按端口兼容度分档：有对得上端口的类型优先；档内再按普通打分。
  std::vector<Hit> hits = search(query, 0);
  for (Hit& hit : hits) {
    int tier = 0;  // None
    const auto ports = as_input ? hit.type->inputs : hit.type->outputs;
    for (const PortSpec& spec : ports) {
      const PortMatch m = as_input ? port_match(port, spec.type) : port_match(spec.type, port);
      if (m != PortMatch::None) {
        tier = std::max(tier, static_cast<int>(m));
      }
    }
    hit.score = tier * 1000 + hit.score % 1000;  // 兼容度优先，原打分做次序。
  }
  std::stable_sort(hits.begin(), hits.end(),
                   [](const Hit& a, const Hit& b) { return a.score > b.score; });
  if (limit != 0 && hits.size() > limit) {
    hits.resize(limit);
  }
  return hits;
}

}  // namespace tamias
