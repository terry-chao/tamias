#pragma once

#include <functional>
#include <string_view>

namespace tamias {

// 会话层事件：壳把它们翻译成 Qt 信号 / JS 回调。
enum class HostEvent {
  DocumentChanged,   // 命令 / undo / redo 后文档内容变化
  SelectionChanged,  // 选择集变化
  ToolChanged,       // 工具模式变化
  StatusMessage,     // 会话层产生的提示文本
  ConsoleMessage,    // 命令回显：一条已执行调用的等价 C# 文本（见 host/command_echo.h）
};

using HostListener = std::function<void(HostEvent, std::string_view)>;

}  // namespace tamias
