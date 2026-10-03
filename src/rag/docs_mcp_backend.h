#pragma once

#include "mcp/mcp_backend.h"
#include "rag/context_provider.h"

#include <string>
#include <string_view>
#include <vector>

namespace tamias::rag {

// 把 ContextProvider 翻成 MCP 的工具与资源。协议层不认识 rag，rag 也不认识 MCP，
// 两边各自能单独测（PLAN-RAG §4.1）。
//
// 全是只读：mutates = false，所以「只读」策略档下直接放行，也没有审批与撤销。
// 不持有 Session，物理上改不了模型。
class DocsMcpBackend final : public mcp::McpBackend {
 public:
  // 与 SessionMcpBackend 的工具名不会撞车；装配处如果撞了，CompositeBackend 会报错。
  static constexpr std::string_view kSearchTool = "tamias_search_docs";
  static constexpr std::string_view kIndexResource = "tamias://docs/index";

  // expected_rev 非空且与索引 manifest 里的 git_rev 不符时，工具结果会附一句
  // 「索引可能过期」。装配处传编译期烘进来的 rev（build_git_rev()），
  // 打包好的程序被换上另一个版本的索引时，模型才能察觉自己拿的是旧知识。
  explicit DocsMcpBackend(const ContextProvider& provider, std::string expected_rev = {});

  [[nodiscard]] bool index_stale() const;

  [[nodiscard]] std::vector<mcp::McpTool> tools() const override;
  [[nodiscard]] std::vector<mcp::McpResource> resources() const override;
  [[nodiscard]] std::vector<mcp::McpResourceTemplate> resource_templates() const override {
    return {};
  }
  [[nodiscard]] mcp::McpToolResult call_tool(std::string_view name,
                                             const mcp::Json& args) override;
  [[nodiscard]] std::optional<mcp::McpResourceContent> read_resource(
      std::string_view uri) override;

 private:
  [[nodiscard]] mcp::McpToolResult search(const mcp::Json& args) const;

  const ContextProvider& provider_;
  // 从 manifest_json() 里解出来缓存住 —— ContextProvider 接口刻意只有
  // search / manifest_json 两个方法，不为这一个字段再加一个 getter。
  std::string git_rev_;
  std::string expected_rev_;
};

// 编译期烘进来的 git rev，用来判断索引是不是跟这个二进制同一个版本。
// 取不到（没装 git、从 tar 包构建）就是空串，此时不做新鲜度判断。
[[nodiscard]] std::string build_git_rev();

}  // namespace tamias::rag
