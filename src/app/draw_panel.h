#pragma once

#include "command/command_system.h"
#include "component_specs.h"
#include "host/tool_mode.h"

#include <QString>
#include <QWidget>
#include <unordered_map>

class QButtonGroup;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace tamias {

class SectionPreviewWidget;

// 绘制设置面板（左侧 QDockWidget）：点 Ribbon 构件 icon 后弹出，
// 让用户先选子类型、改参数，再"开始绘制"武装命令——替代"点 icon 直接画"。
//
// 流程：set_component(mode) 加载规格 → 用户配置 → 点"开始绘制"（或改参数自动重武装）
// → emit armed_args(mode, args) → 由 DocumentViewport 取消旧 pending 并按 args dispatch。
//
// 面板自己不管显示/隐藏，由 MainWindow 在"选中构件工具 / 视图·面板菜单 / 关闭文档"时切换：
// 没有打开的文档时面板没有意义，必须收起。
class DrawPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit DrawPanel(QWidget* parent = nullptr);

  // 加载某构件的规格并重建表单；mode == None 显示占位。
  void set_component(ToolMode mode);
  [[nodiscard]] ToolMode current_mode() const { return current_mode_; }
  // 当前是否真的是一张可配置的构件表单（草图工具没有规格，面板只显示占位）。
  [[nodiscard]] bool has_component() const { return spec_ != nullptr; }
  [[nodiscard]] bool is_armed() const { return armed_; }
  void set_armed(bool armed);
  // 只同步按钮外观、不发 disarmed：切换文档时清掉"武装"假象，
  // 不去动其它视口里已经 dispatch 的 pending 命令。
  void clear_armed();

 signals:
  // 用户确认参数后发出，视口据此 dispatch 命令（武装）。
  void armed_args(ToolMode mode, const CommandArgs& args);
  void disarmed();  // 取消武装（结束绘制 / 切工具 / Esc）

 private:
  void rebuild_form();
  void rebuild_param_rows();
  void gather_and_emit(bool arm);
  void update_arm_button();
  [[nodiscard]] CommandArgs gather_args() const;
  void on_sub_type_changed(const QString& id);
  void arm_clicked();

  QVBoxLayout* root_ = nullptr;
  QWidget* content_ = nullptr;
  QLabel* title_label_ = nullptr;
  QLabel* discipline_label_ = nullptr;
  QWidget* sub_type_row_ = nullptr;
  QButtonGroup* sub_type_group_ = nullptr;
  QLabel* section_label_ = nullptr;
  SectionPreviewWidget* section_preview_ = nullptr;
  QWidget* param_host_ = nullptr;
  QFormLayout* param_form_ = nullptr;
  QLabel* hint_label_ = nullptr;
  QPushButton* arm_button_ = nullptr;

  ToolMode current_mode_ = ToolMode::None;
  const ComponentSpec* spec_ = nullptr;
  QString current_sub_type_;
  // 参数 spinbox：key -> spinbox（公共 + 当前子类型专属）。
  // 用 std::string 做 key：QString 无 std::hash 特化。
  std::unordered_map<std::string, QDoubleSpinBox*> param_spins_;
  bool armed_ = false;
  bool suppress_rearm_ = false;
};

}  // namespace tamias
