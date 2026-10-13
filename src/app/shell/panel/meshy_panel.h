#pragma once

#include "app/ai/meshy_client.h"

#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QToolButton;

namespace tamias {

// AI 生成 3D（Meshy）面板：一句提示词 → 生成 → 下载 → 作为导入网格进当前文档。
// **实验性功能**，边界写在面板起始说明里：接口字段没对着真实服务核过、目前只接了
// 文字生成 3D、生成结果只作参考几何（不可参数化），手写 GLB 解析器的尺度与朝向也待对账。
//
// 与 AI 助手面板的分工：那个是「模型发起的编辑」，这个是「外部服务生成几何」。
// 唯一的写路径仍然是那条可撤销的导入命令，所以这里**不过 MCP 写策略闸门**——
// 是人在自己点，和工具条、命令控制台同一性质（`--mcp-policy` 约束的是外部 AI 客户端）。
//
// 面板不碰 Document：导入通过 import_handler 交给 MainWindow（只有它知道当前文档是哪个），
// 于是「没打开文档」「切了页签」都不用面板自己处理。
class MeshyPanel final : public QWidget {
  Q_OBJECT
 public:
  // 导入回调：返回新建的节点 id，0 = 没导成（原因由宿主写日志）。
  using ImportHandler = std::function<std::uint64_t(const QString& path, const QString& name)>;

  explicit MeshyPanel(QWidget* parent = nullptr);

  void set_import_handler(ImportHandler handler);
  // 直接把一句提示词发出去（命令行 --meshy-prompt / 以后的脚本自动化用）。
  void generate(const QString& prompt);

 private:
  void submit();
  void cancel();
  void refine();
  void open_settings();
  void fetch_balance();
  void sync_config();
  void set_busy(bool busy);
  void show_stage(ai::MeshyClient::Stage stage);
  void note(const QString& text);

  ai::MeshyClient* client_ = nullptr;
  ImportHandler import_handler_;
  QPlainTextEdit* prompt_ = nullptr;
  QComboBox* art_style_ = nullptr;
  QComboBox* polycount_ = nullptr;  // 可编辑：常用面数在下拉里，也能自己敲
  QProgressBar* progress_ = nullptr;
  QLabel* stage_label_ = nullptr;
  QLabel* balance_label_ = nullptr;
  QLabel* result_ = nullptr;
  QToolButton* generate_button_ = nullptr;
  QToolButton* refine_button_ = nullptr;
  QToolButton* cancel_button_ = nullptr;
  QToolButton* balance_button_ = nullptr;
  QToolButton* settings_button_ = nullptr;
  QString api_key_;
  QString last_prompt_;
  // 细化要用上一次 preview 的任务 id；细化出来的那一版不再接受二次细化。
  QString preview_task_id_;
  bool last_task_was_preview_ = false;
};

}  // namespace tamias
