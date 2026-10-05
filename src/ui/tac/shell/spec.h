#pragma once

#include <string>
#include <vector>

namespace tac {

// 声明式外壳模型：界面"有什么"是数据，界面"长什么样"由后端决定。
// 这是 MainWindow 从手搓 QAction 瘦下来的关键——见 docs/TAC.md 阶段 2。

struct MenuItemSpec {
  std::string command_id;   // 空 = 分隔符或不带动作的父菜单
  std::string label;        // 覆盖命令标题（为空则用 CommandInfo::title）
  bool separator = false;
  std::vector<MenuItemSpec> children;
};

struct MenuSpec {
  std::string id;
  std::string title;
  std::vector<MenuItemSpec> items;
};

struct RibbonItemSpec {
  std::string command_id;
  bool large = false;
};

struct RibbonGroupSpec {
  std::string id;
  std::string title;
  std::vector<RibbonItemSpec> items;
};

struct RibbonPageSpec {
  std::string id;
  std::string title;
  std::vector<RibbonGroupSpec> groups;
};

enum class DockArea {
  Left,
  Right,
  Top,
  Bottom,
  Floating,
};

struct DockSpec {
  std::string id;
  std::string title;
  DockArea area = DockArea::Right;
  bool visible = true;
  std::string toggle_command_id;  // 对应的显隐命令（可为空）
};

// 一个壳（主窗口）的完整声明。后端照着它建窗口骨架。
struct ShellSpec {
  std::string title;
  std::vector<MenuSpec> menus;
  std::vector<RibbonPageSpec> ribbon_pages;
  std::vector<DockSpec> docks;
};

}  // namespace tac
