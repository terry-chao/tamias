#pragma once

#include "command/core/command_system.h"

#include <string>
#include <string_view>

namespace tamias {

// 命令回显：把一次「真正执行」的调用渲染成可直接粘贴进 C# 控制台 / 插件的一行代码：
//
//   host.Dispatch("create_wall", new CommandArgs()
//       .SetPoints("points", [new PickPoint(0f, 0f, 0f, 0), new PickPoint(5f, 0f, 0f, 0)])
//       .SetDouble("height", 3)
//       .SetDouble("thickness", 0.2));
//
// 参数按名字排序，保证同一调用每次回显一致（CommandArgs 是无序表）。
// 这是 parse_command_arg_text（ABI 的 `i:k=v;...` 文本）的反向呈现：那边是机器读，
// 这边是给人看、给人抄。内核不认识这段文本，回显纯属宿主行为。
[[nodiscard]] std::string format_dispatch_call(std::string_view name, const CommandArgs& args);

}  // namespace tamias
