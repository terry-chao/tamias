#pragma once

#include <QDialog>

class QPlainTextEdit;

namespace tamias {

// 图形诊断：把「这台机器上渲染到底怎么决定的」摊开给用户 / IT 看，并能一键复制。
//
// 内容 = 启动时的探测报告（后端、适配器、驱动版本、块名单命中、降级与否）
//      + 策略与块名单状态
//      + 最近日志（客户不用去找日志文件）
//
// 注意：这里**不重新探测**。volk 是单设备模型，进程里已有渲染设备时再建 Vulkan 设备会被
// 守卫拒绝；启动那份报告才是可信快照（见 rhi_diagnostics.h）。
class GraphicsDiagnosticsDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit GraphicsDiagnosticsDialog(QWidget* parent = nullptr);

 private:
  void copy_to_clipboard();
  void save_to_file();
  [[nodiscard]] QString build_report_text() const;

  QPlainTextEdit* report_view_ = nullptr;
};

}  // namespace tamias
