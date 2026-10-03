#pragma once

#include "mcp/mcp_backend.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tamias::mcp {

// 把多个后端合成一个。McpServer 只收一个 McpBackend&，所以「文档读写」与
// 「文档检索」必须在这里合流（PLAN-RAG §4.2）。
//
// 重名是**硬错误**，不是后到者胜：两个后端都提供 `tamias_undo` 的话，路由到谁
// 取决于注册顺序，这种 bug 在结果上几乎看不出来。装配处应当据此启动失败。
//
// 不持有后端，调用方保证它们活过本对象。
class CompositeBackend final : public McpBackend {
 public:
  // 合并工具 / 资源 / 模板。返回 false 并写 reason 表示有重名。
  bool add(McpBackend& backend, std::string* error = nullptr);

  [[nodiscard]] std::vector<McpTool> tools() const override;
  [[nodiscard]] std::vector<McpResource> resources() const override;
  [[nodiscard]] std::vector<McpResourceTemplate> resource_templates() const override;
  [[nodiscard]] McpToolResult call_tool(std::string_view name, const Json& args) override;
  [[nodiscard]] std::optional<McpResourceContent> read_resource(
      std::string_view uri) override;

  [[nodiscard]] std::size_t backend_count() const { return backends_.size(); }
  [[nodiscard]] bool empty() const { return backends_.empty(); }

 private:
  std::vector<McpBackend*> backends_;
  std::unordered_map<std::string, McpBackend*> tool_routes_;
  std::unordered_map<std::string, McpBackend*> resource_routes_;
};

}  // namespace tamias::mcp
