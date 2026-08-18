#pragma once

#include <QString>
#include <QWidget>
#include <cstdint>

class QLabel;
class QPlainTextEdit;

namespace tamias {

class Document;

// 调试窗口：显示当前选中构件在 .tdoc 里的句柄（entity / scene node id）。
class HandleInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit HandleInspector(QWidget* parent = nullptr);

  void show_selection(const Document* document, std::uint64_t node_id);

 private:
  void set_empty(const QString& note);

  QLabel* handle_ = nullptr;
  QLabel* kind_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* mesh_ = nullptr;
  QPlainTextEdit* relations_ = nullptr;
};

}  // namespace tamias
