#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tac {

// 这套枚举与 C# 侧 Tamias.Api 的 DialogButtons / DialogResult / PromptFieldKind
// 数值一一对应（插件 API 不变）。见 docs/plugin/api/ui.md。
enum class DialogButtons : std::int32_t {
  Ok = 0,
  OkCancel = 1,
  YesNo = 2,
  YesNoCancel = 3,
};

enum class DialogResult : std::int32_t {
  None = 0,
  Ok = 1,
  Cancel = 2,
  Yes = 3,
  No = 4,
};

enum class PromptFieldKind : std::int32_t {
  String = 0,
  Number = 1,
  Bool = 2,
};

struct PromptField {
  PromptFieldKind kind = PromptFieldKind::String;
  std::string id;
  std::string label;
  std::string text;
  double number = 0.0;
  double min = 0.0;
  double max = 0.0;
  bool has_range = false;
  bool flag = false;
};

// 多字段表单。字段 id 用于回读取值；语义与 C# 侧 PromptForm 一致：
// min/max 都给才限制范围，只给一个不生效。
class PromptForm {
 public:
  std::string title;

  PromptForm& add_string(std::string id, std::string label, std::string value = {});
  PromptForm& add_number(std::string id, std::string label, double value,
                         std::optional<double> min = std::nullopt,
                         std::optional<double> max = std::nullopt);
  PromptForm& add_bool(std::string id, std::string label, bool value = false);

  [[nodiscard]] const std::vector<PromptField>& fields() const { return fields_; }
  [[nodiscard]] std::vector<PromptField>& fields() { return fields_; }

  [[nodiscard]] std::string string_value(std::string_view id) const;
  [[nodiscard]] double number_value(std::string_view id) const;
  [[nodiscard]] bool bool_value(std::string_view id) const;

 private:
  [[nodiscard]] const PromptField& find(std::string_view id) const;

  std::vector<PromptField> fields_;
};

struct FileDialogRequest {
  std::string title;
  std::string filter;        // "IFC (*.ifc);;所有文件 (*.*)"，与 Qt 过滤器写法一致
  std::string default_name;  // save_file 的预填文件名
};

// 平台对话框服务：消息框、单字段输入、多字段表单、打开 / 保存文件。
// 模态调用是阻塞的——与现有 Qt 行为一致，别在回调里连着弹好几个。
class DialogService {
 public:
  virtual ~DialogService() = default;

  virtual DialogResult show_message(std::string_view title, std::string_view message,
                                    DialogButtons buttons = DialogButtons::Ok) = 0;

  // 取消返回 nullopt，成功返回用户输入的文本（可能是空串）。
  virtual std::optional<std::string> prompt_string(std::string_view title,
                                                   std::string_view label,
                                                   std::string_view default_value = {}) = 0;

  // 取消返回 nullopt。
  virtual std::optional<double> prompt_number(std::string_view title, std::string_view label,
                                              double default_value = 0.0,
                                              std::optional<double> min = std::nullopt,
                                              std::optional<double> max = std::nullopt) = 0;

  // 返回 false 表示取消；返回 true 时字段值已写回 form。
  virtual bool show_form(PromptForm& form) = 0;

  virtual std::optional<std::string> open_file(const FileDialogRequest& request) = 0;
  virtual std::optional<std::string> save_file(const FileDialogRequest& request) = 0;
};

}  // namespace tac
