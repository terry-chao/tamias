#pragma once

#include <QString>
#include <QWidget>
#include <cstdint>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace tamias {

class Document;

// 调试窗口：显示当前选中构件在 .tdoc 里的句柄（entity / scene node id）。
// 值双击复制到剪贴板；可输入句柄点定位，让视口框显该构件。
class HandleInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit HandleInspector(QWidget* parent = nullptr);

  void show_selection(const Document* document, std::uint64_t node_id);

 signals:
  // 用户输入句柄并点定位；node_id 为 0 表示输入无效。
  void locate_requested(std::uint64_t node_id);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void set_empty(const QString& note);
  void emit_locate();
  void copy_value(QLabel* label);

  QLabel* handle_ = nullptr;
  QLabel* kind_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* mesh_ = nullptr;
  QPlainTextEdit* relations_ = nullptr;
  QLineEdit* handle_input_ = nullptr;
  QPushButton* locate_btn_ = nullptr;
  QLabel* toast_ = nullptr;
};

}  // namespace tamias
