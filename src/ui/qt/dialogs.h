#pragma once

#include "ui/tac/platform/dialogs.h"

class QWidget;

namespace tac::qt {

// DialogService 的 Qt 实现：消息框、单字段输入、多字段表单、打开 / 保存文件。
// 行为对齐既有 app/shell/dialog/plugin_prompt_dialog.*（阶段 1 由它接管那条路径）。
class QtDialogService final : public DialogService {
 public:
  explicit QtDialogService(QWidget* parent = nullptr);

  // 壳在活动窗口变化时调用；空表示用当前活动窗口。
  void set_parent(QWidget* parent) { parent_ = parent; }
  [[nodiscard]] QWidget* parent() const { return parent_; }

  DialogResult show_message(std::string_view title, std::string_view message,
                            DialogButtons buttons = DialogButtons::Ok) override;
  std::optional<std::string> prompt_string(std::string_view title, std::string_view label,
                                           std::string_view default_value = {}) override;
  std::optional<double> prompt_number(std::string_view title, std::string_view label,
                                      double default_value = 0.0,
                                      std::optional<double> min = std::nullopt,
                                      std::optional<double> max = std::nullopt) override;
  bool show_form(PromptForm& form) override;
  std::optional<std::string> open_file(const FileDialogRequest& request) override;
  std::optional<std::string> save_file(const FileDialogRequest& request) override;

 private:
  QWidget* parent_ = nullptr;
};

}  // namespace tac::qt
