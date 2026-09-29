#pragma once

#include <QPlainTextEdit>
#include <QSize>
#include <QWidget>

class QPaintEvent;
class QResizeEvent;

namespace tamias {

class CodeEditor;

// 行号槽（Qt 官方 Code Editor 示例的同一套做法：编辑器增删行时重算宽度）。
class LineNumberArea final : public QWidget {
 public:
  explicit LineNumberArea(CodeEditor* editor);
  [[nodiscard]] QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  CodeEditor* editor_ = nullptr;
};

// 脚本编辑器：等宽 + 行号 + 当前行高亮 + 「跳到第 N 行」。
//
// 只做控制台需要的那点。折叠、补全、多光标是 RoslynPad / VS Code 的活——
// 这里的价值是「敲一段、跑一下、别丢」，不是做成 IDE。
class CodeEditor final : public QPlainTextEdit {
  Q_OBJECT
 public:
  explicit CodeEditor(QWidget* parent = nullptr);

  [[nodiscard]] int line_number_area_width() const;
  void line_number_area_paint_event(QPaintEvent* event);

  // 光标落到第 line 行（1 起）第 column 列（1 起），并滚动到可见处。
  // 求值报错时用它把用户带到出错那行——错误文本里有 `(行,列)`。
  void goto_line(int line, int column = 1);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private slots:
  void update_line_number_area_width(int new_block_count);
  void update_line_number_area(const QRect& rect, int dy);
  void highlight_current_line();

 private:
  LineNumberArea* gutter_ = nullptr;
};

}  // namespace tamias
