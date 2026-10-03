#pragma once

#include <string_view>

namespace tamias {

// AI 写操作的三档策略。**默认只读**：外部客户端第一次连上不会直接改文档，
// 要显式给 `--mcp-policy=ask|auto` 才放写权限。
//
//   kReadOnly  只放读工具；所有写工具被拒（模型会看到原因）
//   kAsk       写工具执行前弹一次确认（Tamias 里的人点「允许」）
//   kAuto      写工具直接执行（白名单/审批留给以后）
enum class McpPolicy { kReadOnly, kAsk, kAuto };

[[nodiscard]] inline const char* mcp_policy_name(McpPolicy policy) {
  switch (policy) {
    case McpPolicy::kReadOnly:
      return "read-only";
    case McpPolicy::kAsk:
      return "ask";
    case McpPolicy::kAuto:
      return "auto";
  }
  return "read-only";
}

[[nodiscard]] inline bool parse_mcp_policy(std::string_view text, McpPolicy& out) {
  if (text == "read-only" || text == "readonly" || text == "ro") {
    out = McpPolicy::kReadOnly;
    return true;
  }
  if (text == "ask") {
    out = McpPolicy::kAsk;
    return true;
  }
  if (text == "auto") {
    out = McpPolicy::kAuto;
    return true;
  }
  return false;
}

}  // namespace tamias
