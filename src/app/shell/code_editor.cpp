#include "app/shell/code_editor.h"

#include <QFontDatabase>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>

namespace tamias {
namespace {

constexpr int kGutterPadding = 12;

}  // namespace

LineNumberArea::LineNumberArea(CodeEditor* editor) : QWidget(editor), editor_(editor) {}

QSize LineNumberArea::sizeHint() const {
  return QSize(editor_->line_number_area_width(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
  editor_->line_number_area_paint_event(event);
}

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
  setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  setLineWrapMode(QPlainTextEdit::NoWrap);
  gutter_ = new LineNumberArea(this);

  connect(this, &QPlainTextEdit::blockCountChanged, this,
          &CodeEditor::update_line_number_area_width);
  connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::update_line_number_area);
  connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlight_current_line);

  update_line_number_area_width(0);
  highlight_current_line();
}

int CodeEditor::line_number_area_width() const {
  int digits = 1;
  for (int max = qMax(1, blockCount()); max >= 10; max /= 10) {
    ++digits;
  }
  return kGutterPadding + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::update_line_number_area_width(int /*new_block_count*/) {
  setViewportMargins(line_number_area_width(), 0, 0, 0);
}

void CodeEditor::update_line_number_area(const QRect& rect, int dy) {
  if (dy != 0) {
    gutter_->scroll(0, dy);
  } else {
    gutter_->update(0, rect.y(), gutter_->width(), rect.height());
  }
  if (rect.contains(viewport()->rect())) {
    update_line_number_area_width(0);
  }
}

void CodeEditor::resizeEvent(QResizeEvent* event) {
  QPlainTextEdit::resizeEvent(event);
  const QRect cr = contentsRect();
  gutter_->setGeometry(QRect(cr.left(), cr.top(), line_number_area_width(), cr.height()));
}

void CodeEditor::line_number_area_paint_event(QPaintEvent* event) {
  QPainter painter(gutter_);
  painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));

  QTextBlock block = firstVisibleBlock();
  int block_number = block.blockNumber();
  int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  int bottom = top + qRound(blockBoundingRect(block).height());
  const int current_line = textCursor().blockNumber();

  while (block.isValid() && top <= event->rect().bottom()) {
    if (block.isVisible() && bottom >= event->rect().top()) {
      painter.setPen(palette().color(block_number == current_line ? QPalette::Text
                                                                 : QPalette::PlaceholderText));
      painter.drawText(0, top, gutter_->width() - 6, fontMetrics().height(), Qt::AlignRight,
                       QString::number(block_number + 1));
    }
    block = block.next();
    top = bottom;
    bottom = top + qRound(blockBoundingRect(block).height());
    ++block_number;
  }
}

void CodeEditor::highlight_current_line() {
  QList<QTextEdit::ExtraSelection> selections;
  if (!isReadOnly()) {
    QTextEdit::ExtraSelection selection;
    QColor line_color = palette().color(QPalette::Highlight);
    line_color.setAlpha(40);
    selection.format.setBackground(line_color);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    selections.append(selection);
  }
  setExtraSelections(selections);
}

void CodeEditor::goto_line(int line, int column) {
  const QTextBlock block = document()->findBlockByLineNumber(qMax(0, line - 1));
  if (!block.isValid()) {
    return;
  }
  QTextCursor cursor(block);
  const int offset = qMax(0, column - 1);
  cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                      qMin(offset, qMax(0, block.length() - 1)));
  setTextCursor(cursor);
  centerCursor();
  setFocus();
}

}  // namespace tamias
