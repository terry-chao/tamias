#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;
class QSplitter;
class QToolButton;

namespace tamias {

class CodeEditor;

// 命令控制台 / 脚本页：输出面 + 编辑器。
//
// 输出面：每条真正执行的内核命令都显示成等价的 C# 调用——在工具条上画一面墙，
// 这里就出现一行 `host.Dispatch("create_wall", …)`。学 API 不必先读文档。
//
// 编辑器：敲一段 C# 跑（Ctrl+Enter）。求值在 Tamias.Host 的脚本引擎里做，每段自带
// 一个事务，所以改错了按一次 Ctrl+Z 就全退回。脚本存在 <AppData>/scripts，Ctrl+S 保存——
// **不进 .tdoc**：脚本是行为，工作文档是数据。
//
// 回显文本由 host/command_echo.h 生成；这里只负责显示、编辑和存盘，不解释内容。
class ConsolePanel final : public QWidget {
  Q_OBJECT
 public:
  enum class LineStyle {
    Normal,  // 命令回显 / 脚本输出
    Input,   // 用户输入的那一行（前缀 `>`）
    Error,   // 求值失败
  };

  explicit ConsolePanel(QWidget* parent = nullptr);

  // 追加一行。空文本忽略；超出上限的旧行自动淘汰。
  void append_line(const QString& text, LineStyle style = LineStyle::Normal);
  void clear_lines();
  // 错误文本里带 `(行,列)`（Roslyn 的诊断格式）时，把光标带到出错那一行。
  void focus_error_line(const QString& error_text);

 signals:
  // 用户按了「运行」/ Ctrl+Enter。主窗口拿去交给脚本引擎求值。
  void run_requested(const QString& code);

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void apply_theme();
  void copy_all();
  void submit();

  // ── 脚本文件 ──
  void refresh_script_list();
  void select_script(const QString& path);
  void update_script_title();
  void new_script();
  void clear_to_untitled();  // new_script 的「确认过了」版本：下拉切到未命名时直接调它
  void open_script_dialog();
  void open_script(const QString& path);
  bool save_script();  // 还没落过盘时直接存进脚本目录（不弹框）
  bool save_script_as();
  // 有未保存改动时先问一句。false = 用户取消，调用方要停下。
  bool confirm_discard();
  [[nodiscard]] bool dirty() const;

  QPlainTextEdit* view_ = nullptr;
  CodeEditor* editor_ = nullptr;
  QComboBox* script_combo_ = nullptr;
  QCheckBox* follow_ = nullptr;
  QSplitter* splitter_ = nullptr;
  QString current_path_;
  QString saved_text_;
  bool syncing_ = false;  // 填 / 拨下拉时挡住 currentIndexChanged 的回调
  QColor text_color_;
  QColor input_color_;
  QColor error_color_;
};

}  // namespace tamias
