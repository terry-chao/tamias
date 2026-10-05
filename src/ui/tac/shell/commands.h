#pragma once

#include <string>

namespace tac {

// 快捷键的可移植写法，例如 "Ctrl+Shift+S" / "F2" / "Del"。
// 由后端翻译成 Qt 的 QKeySequence 或自研库自己的键表。
struct Shortcut {
  std::string sequence;
};

// 一条界面命令的元数据。与 src/command 的 CommandSystem 是"同 id"的两面：
// 界面按这份元数据画菜单 / 工具带，点击后按 id 走 Session::dispatch 或本地动作。
struct CommandInfo {
  std::string id;        // 全局唯一，如 "file.save" / "create.wall"
  std::string title;     // 显示名（会被 i18n 覆盖）
  std::string category;  // 查找 / 分组用，如 "文件"
  std::string icon;      // 图标 id 或资源路径，由后端解释（Qt 后端接受 ":/..."）
  std::string tooltip;
  Shortcut shortcut;
  bool checkable = false;
  bool checked = false;
  bool enabled = true;
};

}  // namespace tac
