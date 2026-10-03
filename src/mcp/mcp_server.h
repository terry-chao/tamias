#pragma once

#include "mcp/json.h"
#include "mcp/mcp_backend.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace tamias::mcp {

// 工具闸门：在真正执行前决定放不放行。策略（只读 / 人工确认）挂在这里，
// 于是它跟传输无关——stdio、loopback、内置面板走的是同一道闸。
struct McpToolGateDecision {
  bool allow = true;
  std::string reason;  // allow = false 时给模型看的原因
};

using McpToolGate =
    std::function<McpToolGateDecision(std::string_view tool, const Json& args)>;
// 工具观察者：执行后回调，用于审计。**被闸门拒绝时也会调用**，
// 否则「AI 想改但被拒」这件事就没人看得见。
using McpToolObserver =
    std::function<void(std::string_view tool, const Json& args, const McpToolResult& result)>;

// MCP 门面：JSON-RPC 2.0 信封 + 当前需要的 MCP 方法。
//
// 只做协议，不做传输——stdio / loopback HTTP / 内置面板都调到 handle()。
// 这样「工具语义」只有一份，换传输不会改工具。
class McpServer {
 public:
  McpServer(McpBackend& backend, std::string server_name, std::string server_version);

  // 处理一条已解析的 JSON-RPC 消息。
  //   - 请求（有 id）→ 返回响应对象
  //   - 通知（无 id）→ 返回 nullopt，不回包
  //   - 批量数组 → 返回响应数组（全是通知则 nullopt）
  [[nodiscard]] std::optional<Json> handle(const Json& message);

  // 处理一段文本：解析失败回 JSON-RPC 的 parse error（id = null）。
  [[nodiscard]] std::optional<Json> handle_text(std::string_view text);

  // 本实现支持的 MCP 协议版本（initialize 时与客户端协商）。
  [[nodiscard]] static std::string_view supported_protocol_version() {
    return "2025-06-18";
  }

  // JSON-RPC 标准错误码。
  static constexpr int kParseError = -32700;
  static constexpr int kInvalidRequest = -32600;
  static constexpr int kMethodNotFound = -32601;
  static constexpr int kInvalidParams = -32602;
  static constexpr int kInternalError = -32603;

  void set_tool_gate(McpToolGate gate) { tool_gate_ = std::move(gate); }
  void set_tool_observer(McpToolObserver observer) { tool_observer_ = std::move(observer); }

 private:
  [[nodiscard]] std::optional<Json> dispatch(const Json& message);
  [[nodiscard]] Json handle_initialize(const Json& params, const Json& id);
  [[nodiscard]] Json handle_tools_list(const Json& id);
  [[nodiscard]] Json handle_tools_call(const Json& params, const Json& id);
  [[nodiscard]] Json handle_resources_list(const Json& id);
  [[nodiscard]] Json handle_resources_templates_list(const Json& id);
  [[nodiscard]] Json handle_resources_read(const Json& params, const Json& id);

  [[nodiscard]] static Json make_result(Json id, Json result);
  [[nodiscard]] static Json make_error(Json id, int code, std::string message);
  [[nodiscard]] static Json content_of(const McpToolResult& result);

  McpBackend& backend_;
  std::string server_name_;
  std::string server_version_;
  McpToolGate tool_gate_;
  McpToolObserver tool_observer_;
};

}  // namespace tamias::mcp
