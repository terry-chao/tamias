#pragma once

#include "engine/base/result.h"
#include "mcp/json.h"

#include <string>

namespace tamias {

// 把 MCP 工具的结构化参数编码成命令参数文本协议（i:/d:/s:/v:/p:/a:）。
// 是 host/command_arg_text.h 的反方向：那边把文本解析成 CommandArgs，
// 这边把 JSON 编码成文本，中间复用同一个解析器，编码规则只有一份。
//
// 规则（刻意无歧义，不看 key 猜类型）：
//   - 整数 → `i:`，非整数 → `d:`，字符串 → `s:`，bool → `i:`(0/1)
//   - `{x,y,z}` → `v:`（单点）
//   - `[{x,y,z}, …]` → `p:`（点列）
//   - `[数字, …]` → `a:`（双精度数组；所以 `ids` / `weights` 不会有歧义）
//
// 失败即报错，不静默丢参数——模型给错类型时应该看到原因，而不是命令悄悄用默认值。
[[nodiscard]] Result<std::string> format_command_arg_text(const mcp::Json& args);

}  // namespace tamias
