#include "app/shell/console_panel.h"

#include "app/base/theme.h"
#include "app/base/script_store.h"
#include "app/shell/code_editor.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace tamias {
namespace {

// 只留最近这些行：控制台是「刚才做了什么」的窗口，不是日志归档。
constexpr int kMaxLines = 2000;

QString console_stylesheet(bool dark) {
  const ThemePalette palette = theme_palette(dark);
  const QColor field = dark ? QColor(30, 31, 34) : QColor(255, 255, 255);
  return QStringLiteral(
             "QPlainTextEdit#consoleView {"
             "  background: $field; color: $text; border: 1px solid $border;"
             "  border-radius: 4px; padding: 4px;"
             "}"
             "QPlainTextEdit#consoleInput {"
             "  background: $field; color: $text; border: 1px solid $border;"
             "  border-radius: 4px; padding: 4px;"
             "}"
             "QToolButton#consoleTool {"
             "  padding: 4px 8px; border-radius: 4px; border: none; color: $text;"
             "  background: transparent;"
             "}"
             "QToolButton#consoleTool:hover { background: $hover; }")
      .replace(QStringLiteral("$field"), css_color(field))
      .replace(QStringLiteral("$text"), css_color(palette.text))
      .replace(QStringLiteral("$border"), css_color(palette.border))
      .replace(QStringLiteral("$hover"), css_color(palette.hover));
}

}  // namespace

ConsolePanel::ConsolePanel(QWidget* parent) : QWidget(parent) {
  view_ = new QPlainTextEdit(this);
  view_->setObjectName(QStringLiteral("consoleView"));
  view_->setReadOnly(true);
  view_->setLineWrapMode(QPlainTextEdit::NoWrap);
  view_->setMaximumBlockCount(kMaxLines);  // 旧行自动淘汰，不会吃内存
  view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  view_->setPlaceholderText(
      tr("Every command you run shows up here as a C# call you can copy back."));

  follow_ = new QCheckBox(tr("Follow"), this);
  follow_->setChecked(true);
  follow_->setToolTip(tr("Scroll to the newest command"));

  auto* copy = new QToolButton(this);
  copy->setObjectName(QStringLiteral("consoleTool"));
  copy->setText(tr("Copy All"));
  copy->setToolTip(tr("Copy every line to the clipboard"));
  connect(copy, &QToolButton::clicked, this, &ConsolePanel::copy_all);

  auto* clear = new QToolButton(this);
  clear->setObjectName(QStringLiteral("consoleTool"));
  clear->setText(tr("Clear"));
  clear->setToolTip(tr("Clear the echo"));
  connect(clear, &QToolButton::clicked, this, &ConsolePanel::clear_lines);

  auto* header = new QHBoxLayout();
  header->setContentsMargins(8, 4, 8, 0);
  header->setSpacing(6);
  header->addWidget(new QLabel(tr("Command Echo"), this));
  header->addStretch(1);
  header->addWidget(follow_);
  header->addWidget(copy);
  header->addWidget(clear);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(4);
  root->addLayout(header);

  const auto make_tool = [this](const QString& text, const QString& tip) {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("consoleTool"));
    button->setText(text);
    button->setToolTip(tip);
    return button;
  };

  // 脚本条：选脚本 + 新建 / 打开 / 保存 / 另存为 + 运行。
  script_combo_ = new QComboBox(this);
  script_combo_->setMinimumWidth(140);
  script_combo_->setToolTip(tr("Scripts in the Tamias scripts folder"));
  connect(script_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (syncing_) {
      return;
    }
    const QString path = script_combo_->itemData(index).toString();
    if (path == current_path_) {
      return;
    }
    if (!confirm_discard()) {
      select_script(current_path_);  // 用户取消：把选择拨回去
      return;
    }
    if (path.isEmpty()) {
      clear_to_untitled();
    } else {
      open_script(path);
    }
  });

  auto* new_button = make_tool(tr("New"), tr("New script"));
  auto* open_button = make_tool(tr("Open…"), tr("Open a script from disk"));
  auto* save_button = make_tool(tr("Save"), tr("Save the script (Ctrl+S)"));
  auto* save_as_button = make_tool(tr("Save As…"), tr("Save the script under a new name"));
  auto* run_button = make_tool(tr("Run"), tr("Run the snippet (Ctrl+Enter)"));
  connect(new_button, &QToolButton::clicked, this, &ConsolePanel::new_script);
  connect(open_button, &QToolButton::clicked, this, &ConsolePanel::open_script_dialog);
  connect(save_button, &QToolButton::clicked, this, [this] { (void)save_script(); });
  connect(save_as_button, &QToolButton::clicked, this, [this] { (void)save_script_as(); });
  connect(run_button, &QToolButton::clicked, this, &ConsolePanel::submit);

  auto* script_row = new QHBoxLayout();
  script_row->setContentsMargins(8, 0, 8, 0);
  script_row->setSpacing(4);
  script_row->addWidget(script_combo_);
  script_row->addWidget(new_button);
  script_row->addWidget(open_button);
  script_row->addWidget(save_button);
  script_row->addWidget(save_as_button);
  script_row->addStretch(1);
  script_row->addWidget(run_button);
  root->addLayout(script_row);

  // 编辑器：敲一段 C#，Ctrl+Enter 跑。每段自带一个事务（见 Tamias.Host/ScriptEngine.cs），
  // 所以这里不需要「确认」——改错了按一次 Ctrl+Z 就全退回。
  editor_ = new CodeEditor(this);
  editor_->setMinimumHeight(80);
  editor_->setPlaceholderText(tr("C# here, Ctrl+Enter to run. `host` is the document host; "
                                 "each run is one undo step."));
  for (const QString& sequence : {QStringLiteral("Ctrl+Return"), QStringLiteral("Ctrl+Enter")}) {
    auto* run_shortcut = new QShortcut(QKeySequence(sequence), editor_);
    connect(run_shortcut, &QShortcut::activated, this, &ConsolePanel::submit);
  }
  auto* save_shortcut = new QShortcut(QKeySequence::Save, editor_);
  connect(save_shortcut, &QShortcut::activated, this, [this] { (void)save_script(); });
  connect(editor_, &QPlainTextEdit::textChanged, this, &ConsolePanel::update_script_title);

  // 输出和编辑器上下分：拖动分隔条调比例。输出默认占大头——回显是主角。
  splitter_ = new QSplitter(Qt::Vertical, this);
  splitter_->addWidget(view_);
  splitter_->addWidget(editor_);
  splitter_->setChildrenCollapsible(false);
  splitter_->setStretchFactor(0, 3);
  splitter_->setStretchFactor(1, 2);
  root->addWidget(splitter_, 1);

  refresh_script_list();
  // 上次编辑的脚本还在就接着编；否则给一段能立刻跑的示例当起手式。
  const QString last = QSettings().value(QStringLiteral("console/script")).toString();
  if (!last.isEmpty() && QFileInfo::exists(last)) {
    open_script(last);
  } else {
    // 起手式是**代码**，不做翻译——翻译代码只会让它不再是能跑的那段。
    editor_->setPlainText(QStringLiteral(
        "// `host` is the document host. Each run is one undo step.\n"
        "host.Log($\"entities {host.Entities.Count}, selected {host.Selection.Count}\");"));
    saved_text_ = editor_->toPlainText();
    select_script(QString());
  }

  apply_theme();
}

void ConsolePanel::append_line(const QString& text, LineStyle style) {
  if (text.isEmpty()) {
    return;
  }
  QTextCursor cursor(view_->document());
  cursor.movePosition(QTextCursor::End);
  if (!view_->document()->isEmpty()) {
    cursor.insertBlock();
  }
  QTextCharFormat format;
  switch (style) {
    case LineStyle::Input:
      format.setForeground(input_color_);
      break;
    case LineStyle::Error:
      format.setForeground(error_color_);
      break;
    case LineStyle::Normal:
      format.setForeground(text_color_);
      break;
  }
  cursor.setCharFormat(format);
  cursor.insertText(text);
  if (follow_ != nullptr && follow_->isChecked()) {
    QScrollBar* bar = view_->verticalScrollBar();
    bar->setValue(bar->maximum());
  }
}

void ConsolePanel::submit() {
  const QString code = editor_->toPlainText().trimmed();
  if (code.isEmpty()) {
    return;
  }
  emit run_requested(code);
}

void ConsolePanel::focus_error_line(const QString& error_text) {
  // Roslyn 的诊断形如 "(3,12): error CS1002: …"。
  static const QRegularExpression pattern(QStringLiteral(R"(\((\d+),(\d+)\))"));
  const QRegularExpressionMatch match = pattern.match(error_text);
  if (match.hasMatch()) {
    editor_->goto_line(match.captured(1).toInt(), match.captured(2).toInt());
  }
}

bool ConsolePanel::dirty() const {
  return editor_->toPlainText() != saved_text_;
}

bool ConsolePanel::confirm_discard() {
  if (!dirty()) {
    return true;
  }
  const auto answer = QMessageBox::question(
      this, tr("Script"), tr("Save changes to the current script?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (answer == QMessageBox::Cancel) {
    return false;
  }
  if (answer == QMessageBox::Save) {
    return save_script();
  }
  return true;
}

void ConsolePanel::refresh_script_list() {
  syncing_ = true;
  script_combo_->clear();
  script_combo_->addItem(tr("Untitled"), QString());
  for (const QString& path : list_scripts()) {
    script_combo_->addItem(QFileInfo(path).fileName(), path);
  }
  syncing_ = false;
}

void ConsolePanel::select_script(const QString& path) {
  syncing_ = true;
  const int index = path.isEmpty() ? 0 : script_combo_->findData(path);
  script_combo_->setCurrentIndex(index >= 0 ? index : 0);
  syncing_ = false;
  update_script_title();
}

void ConsolePanel::update_script_title() {
  const QString base =
      current_path_.isEmpty() ? tr("Untitled") : QFileInfo(current_path_).fileName();
  const QString title = dirty() ? base + QStringLiteral(" *") : base;
  syncing_ = true;
  const int index = script_combo_->currentIndex();
  if (index >= 0) {
    script_combo_->setItemText(index, title);
  }
  syncing_ = false;
  editor_->setToolTip(current_path_.isEmpty()
                          ? tr("Not saved yet — Save puts it in the scripts folder")
                          : current_path_);
}

void ConsolePanel::new_script() {
  if (!confirm_discard()) {
    return;
  }
  clear_to_untitled();
}

void ConsolePanel::clear_to_untitled() {
  current_path_.clear();
  editor_->setPlainText(QString());
  saved_text_.clear();
  select_script(QString());
  editor_->setFocus();
}

void ConsolePanel::open_script_dialog() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open Script"), scripts_directory(),
      tr("Tamias scripts (*.cs);;All files (*.*)"));
  if (path.isEmpty() || !confirm_discard()) {
    return;
  }
  open_script(path);
}

void ConsolePanel::open_script(const QString& path) {
  QString text;
  QString error;
  if (!read_script(path, text, error)) {
    append_line(tr("Cannot read %1: %2").arg(path, error), LineStyle::Error);
    return;
  }
  current_path_ = path;
  editor_->setPlainText(text);
  saved_text_ = editor_->toPlainText();  // 读进来不算改动
  refresh_script_list();
  select_script(path);
  QSettings().setValue(QStringLiteral("console/script"), path);
}

bool ConsolePanel::save_script() {
  if (current_path_.isEmpty()) {
    const QString candidate = next_script_path();
    if (candidate.isEmpty()) {
      append_line(tr("Scripts folder is not available."), LineStyle::Error);
      return false;
    }
    current_path_ = candidate;  // 「保存」第一次就是存进脚本目录，不弹框
  }
  QString error;
  if (!write_script(current_path_, editor_->toPlainText(), error)) {
    append_line(tr("Cannot save %1: %2").arg(current_path_, error), LineStyle::Error);
    return false;
  }
  saved_text_ = editor_->toPlainText();
  refresh_script_list();
  select_script(current_path_);
  QSettings().setValue(QStringLiteral("console/script"), current_path_);
  append_line(tr("Saved %1").arg(current_path_));
  return true;
}

bool ConsolePanel::save_script_as() {
  const QString suggestion =
      current_path_.isEmpty() ? next_script_path() : current_path_;
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save Script As"), suggestion,
      tr("Tamias scripts (*.cs);;All files (*.*)"));
  if (path.isEmpty()) {
    return false;
  }
  current_path_ = path;
  return save_script();
}

void ConsolePanel::clear_lines() {
  view_->clear();
}

void ConsolePanel::copy_all() {
  if (QClipboard* clipboard = QGuiApplication::clipboard()) {
    clipboard->setText(view_->toPlainText());
  }
}

void ConsolePanel::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event != nullptr &&
      (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange)) {
    apply_theme();
  }
}

void ConsolePanel::apply_theme() {
  const bool dark = is_dark_theme();
  const ThemePalette palette = theme_palette(dark);
  text_color_ = palette.text;
  input_color_ = palette.accent;
  error_color_ = dark ? QColor(240, 122, 112) : QColor(188, 44, 44);
  setStyleSheet(console_stylesheet(dark));
}

}  // namespace tamias
