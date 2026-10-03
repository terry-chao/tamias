#pragma once

#include "host/session.h"
#include "engine/base/result.h"
#include "mcp/mcp_backend.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace tamias {

// 把 Session 暴露成 MCP 工具 / 资源。协议层（mcp::McpServer）不认识它，
// 所以桌面、Web、无头都能共用同一个后端。
//
// 写路径和插件完全一致：只会 dispatch，绝不直接改文档。交互式命令点没给齐时
// 仍然只是「武装工具」，由用户在视口完成——见 docs/DECISION-AI-INTEGRATION.md §6。
class SessionMcpBackend final : public mcp::McpBackend {
 public:
  // 允许 nullptr：停在欢迎页时没有活动文档，工具仍需可列出，读接口返回空表、
  // 写接口报 "no active document"——和插件侧的「无文档时」行为一致。
  explicit SessionMcpBackend(Session* session = nullptr) : session_(session) {}
  void set_session(Session* session) { session_ = session; }
  [[nodiscard]] bool has_document() const { return session_ != nullptr; }
  // 写工具成功后的回调（壳用它刷新视口）。和 PluginHost::bind 的 after_edit 同义，
  // 所以 AI 改动和手点按钮一样会立刻反映到画面。
  void set_after_edit(std::function<void()> after_edit) {
    after_edit_ = std::move(after_edit);
  }
  // 可选逃生舱：直接求值一段 C#（走壳里的 Tamias.Host 脚本引擎）。
  // 不设置时 tamias_evaluate 不在工具表里——默认关闭，不是"列出来但报错"。
  void set_evaluate(std::function<Result<std::string>(std::string_view)> evaluate) {
    evaluate_ = std::move(evaluate);
  }
  // 这个工具会不会改文档。策略闸门用它区分「读」和「写」，
  // 不依赖调用方自己维护一份工具名单。
  [[nodiscard]] static bool mutates(std::string_view tool);

  [[nodiscard]] std::vector<mcp::McpTool> tools() const override;
  [[nodiscard]] std::vector<mcp::McpResource> resources() const override;
  [[nodiscard]] std::vector<mcp::McpResourceTemplate> resource_templates() const override;
  [[nodiscard]] mcp::McpToolResult call_tool(std::string_view name,
                                             const mcp::Json& args) override;
  [[nodiscard]] std::optional<mcp::McpResourceContent> read_resource(
      std::string_view uri) override;

 private:
  [[nodiscard]] mcp::McpToolResult call_tool_impl(std::string_view name, const mcp::Json& args);

  [[nodiscard]] mcp::Json document_json() const;
  [[nodiscard]] mcp::Json entities_json(std::string_view kind_filter,
                                        std::string_view name_filter, int offset,
                                        int limit) const;
  [[nodiscard]] mcp::Json features_json(std::uint64_t entity_id) const;
  [[nodiscard]] mcp::Json selection_json() const;

  // 发一条命令：JSON 参数 → 文本协议 → CommandArgs → Session::dispatch。
  [[nodiscard]] mcp::McpToolResult dispatch_json_command(std::string_view command,
                                                         const mcp::Json& args);
  [[nodiscard]] mcp::McpToolResult run_batch(const mcp::Json& operations);

  Session* session_ = nullptr;
  std::function<void()> after_edit_;
  std::function<Result<std::string>(std::string_view)> evaluate_;
};

}  // namespace tamias
