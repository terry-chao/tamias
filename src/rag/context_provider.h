#pragma once

// 检索接缝。与 mcp::McpBackend 同级：上面（MCP 门面 / 面板）只认这个接口，
// 下面（词法 / 以后可能的向量后端）随便换。纯 C++，无 Qt、无 engine、无 OCCT，
// 所以能在没有 GUI 的环境里跑 CI 与评测（见 docs/PLAN-RAG.md 不变量 3）。

#include <string>
#include <vector>

namespace tamias::rag {

// 一条检索结果，字段与 resources/rag/chunks.jsonl 一一对应。
struct Chunk {
  std::string id;          // path#序号，语料内唯一
  std::string corpus;      // 现在恒为 "docs"
  std::string path;        // 相对 docs/ 的路径
  std::string anchor;      // 官网 HTML 里的 id；空 = 该页正文不渲染（首页）
  std::string title;
  std::string breadcrumb;
  std::string lang;
  std::string url;
  std::string text;
  double score = 0.0;  // 由检索填充，语料里没有
};

struct Query {
  std::string text;
  std::string corpus;       // 空 = 全部
  std::string lang;         // zh / en，空 = 全部
  std::string path_prefix;
  int k = 5;                // 上限见 kMaxResults
};

// 返回条数上限：再多也塞不进上下文预算（PLAN-RAG §5）。
constexpr int kMaxResults = 20;

class ContextProvider {
 public:
  virtual ~ContextProvider() = default;
  [[nodiscard]] virtual std::vector<Chunk> search(const Query& query) const = 0;
  // 语料清单 JSON（chunk 数 / git_rev / 构建时间），原样来自 manifest.json。
  [[nodiscard]] virtual std::string manifest_json() const = 0;
};

}  // namespace tamias::rag
