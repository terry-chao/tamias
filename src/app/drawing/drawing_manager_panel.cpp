#include "app/drawing/drawing_manager_panel.h"

#include "app/viewport/canvas/document_viewport.h"
#include "app/drawing/drawing_document.h"
#include "app/drawing/drawing_settings_dialog.h"
#include "engine/document/document.h"
#include "app/base/theme.h"

#include <QCheckBox>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace tamias {
namespace {

constexpr int kPathRole = Qt::UserRole;
// 行上的勾 = 这张图纸在三维视口里画不画（写回文档，随 .tdoc 存）。
constexpr int kVisibleColumn = 0;
constexpr int kStatusColumn = 1;

QString panel_stylesheet(bool dark) {
  const ThemePalette palette = theme_palette(dark);
  QString sheet =
      QStringLiteral(
          "QWidget#drawingHeader { background: transparent; border-bottom: 1px solid $divider; }"
          "QLabel#drawingCaption { color: $muted; }"
          "QToolButton#drawingToolButton {"
          "  padding: 3px 8px; border-radius: 4px; border: none; color: $text;"
          "  background: transparent;"
          "}"
          "QToolButton#drawingToolButton:hover { background: $hover; }"
          "QToolButton#drawingToolButton:pressed { background: $checked; }"
          "QToolButton#drawingToolButton:disabled { color: $muted; }"
          "QTreeWidget#drawingTree { background: transparent; border: none; outline: none; }"
          "QTreeWidget#drawingTree::item { padding: 3px 2px; border-radius: 4px; }"
          "QTreeWidget#drawingTree::item:hover { background: $hover; }"
          "QTreeWidget#drawingTree::item:selected { background: $checked; }"
          "QTreeWidget#drawingTree QHeaderView::section {"
          "  background: transparent; color: $muted; border: none;"
          "  border-bottom: 1px solid $divider; padding: 2px 4px; font-size: 11px;"
          "}"
          "QCheckBox#drawingShowAll { color: $text; }"
          "QLabel#drawingHint { color: $muted; padding: 16px 12px; }");
  sheet.replace(QStringLiteral("$divider"), css_color(palette.divider));
  sheet.replace(QStringLiteral("$muted"), css_color(palette.text_muted));
  sheet.replace(QStringLiteral("$text"), css_color(palette.text));
  sheet.replace(QStringLiteral("$hover"), css_color(palette.hover));
  sheet.replace(QStringLiteral("$checked"), css_color(palette.checked));
  return sheet;
}

}  // namespace

DrawingManagerPanel::DrawingManagerPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("drawingPanel"));
  dark_ = is_dark_theme();

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* header = new QWidget(this);
  header->setObjectName(QStringLiteral("drawingHeader"));
  auto* header_layout = new QVBoxLayout(header);
  header_layout->setContentsMargins(10, 8, 10, 6);
  header_layout->setSpacing(2);
  auto* caption = new QLabel(tr("Drawings"), header);
  caption->setObjectName(QStringLiteral("drawingCaption"));
  header_layout->addWidget(caption);
  auto* sub = new QLabel(tr("Attached to this document and drawn under the model"), header);
  sub->setObjectName(QStringLiteral("drawingCaption"));
  sub->setWordWrap(true);
  header_layout->addWidget(sub);

  show_all_ = new QCheckBox(tr("Show drawings in the viewport"), header);
  show_all_->setObjectName(QStringLiteral("drawingShowAll"));
  show_all_->setChecked(true);
  show_all_->setToolTip(
      tr("Master switch — turn it off to hide every reference drawing at once"));
  header_layout->addWidget(show_all_);

  // 面板窄（272 px），按钮分两行：第一行挂 / 删，第二行是查看与设置。
  const auto add_tool_button = [&](QHBoxLayout* row, const QString& text,
                                   const QString& tip) {
    auto* button = new QToolButton(header);
    button->setObjectName(QStringLiteral("drawingToolButton"));
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    row->addWidget(button);
    return button;
  };

  auto* bar = new QHBoxLayout();
  bar->setContentsMargins(0, 4, 0, 0);
  bar->setSpacing(4);
  add_button_ = add_tool_button(
      bar, tr("Add…"),
      tr("Attach DWF / DWFx / DXF / PDF / SVG / image drawings to this document; they are "
         "shown in the viewport and the files stay where they are"));
  remove_button_ = add_tool_button(bar, tr("Delete"),
                                   tr("Unlink the selected drawing (the file is kept)"));
  bar->addStretch(1);
  header_layout->addLayout(bar);

  auto* bar2 = new QHBoxLayout();
  bar2->setContentsMargins(0, 0, 0, 0);
  bar2->setSpacing(4);
  locate_button_ = add_tool_button(bar2, tr("Locate"), tr("Frame this drawing in the viewport"));
  settings_button_ = add_tool_button(
      bar2, tr("Settings…"), tr("Scale, rotation, offset, elevation and page"));
  open_button_ =
      add_tool_button(bar2, tr("2D page"), tr("Open the selected drawing in a read-only 2D page"));
  bar2->addStretch(1);
  header_layout->addLayout(bar2);
  root->addWidget(header);

  pages_ = new QStackedWidget(this);

  tree_ = new QTreeWidget(pages_);
  tree_->setObjectName(QStringLiteral("drawingTree"));
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("Drawing"), tr("Status")});
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  tree_->setColumnWidth(1, 66);
  tree_->setRootIsDecorated(false);
  tree_->setUniformRowHeights(true);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setToolTip(tr("Tick a row to show or hide that drawing in the viewport"));
  pages_->addWidget(tree_);

  hint_ = new QLabel(pages_);
  hint_->setObjectName(QStringLiteral("drawingHint"));
  hint_->setWordWrap(true);
  hint_->setAlignment(Qt::AlignCenter);
  pages_->addWidget(hint_);
  pages_->setCurrentWidget(hint_);
  root->addWidget(pages_, 1);

  connect(add_button_, &QToolButton::clicked, this, &DrawingManagerPanel::add_drawings);
  connect(open_button_, &QToolButton::clicked, this, &DrawingManagerPanel::open_selected_in_2d);
  connect(locate_button_, &QToolButton::clicked, this, &DrawingManagerPanel::locate_selected);
  connect(settings_button_, &QToolButton::clicked, this, &DrawingManagerPanel::edit_selected);
  connect(remove_button_, &QToolButton::clicked, this, &DrawingManagerPanel::remove_selected);
  connect(show_all_, &QCheckBox::toggled, this, &DrawingManagerPanel::on_show_all_toggled);
  connect(tree_, &QTreeWidget::itemChanged, this, &DrawingManagerPanel::on_item_changed);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          &DrawingManagerPanel::on_item_double_clicked);
  connect(tree_, &QTreeWidget::itemSelectionChanged, this, &DrawingManagerPanel::sync_buttons);

  setStyleSheet(panel_stylesheet(dark_));
  refresh();
}

void DrawingManagerPanel::set_dark_theme(bool dark) {
  if (dark_ == dark) {
    return;
  }
  dark_ = dark;
  setStyleSheet(panel_stylesheet(dark_));
  refresh();
}

void DrawingManagerPanel::set_viewport(DocumentViewport* viewport) {
  if (viewport_ != viewport) {
    for (const QMetaObject::Connection& connection : viewport_connections_) {
      QObject::disconnect(connection);
    }
    viewport_connections_.clear();
    viewport_ = viewport;
    if (viewport_ != nullptr) {
      // 队列投递：这些信号可能是在面板自己的槽里（勾选框 / 双击）发出来的，
      // 直接重进 refresh() 会把正在遍历的行删掉。
      viewport_connections_.push_back(connect(viewport_, &DocumentViewport::document_changed,
                                              this, &DrawingManagerPanel::refresh,
                                              Qt::QueuedConnection));
      viewport_connections_.push_back(connect(viewport_, &DocumentViewport::drawings_changed,
                                              this, &DrawingManagerPanel::refresh,
                                              Qt::QueuedConnection));
      viewport_connections_.push_back(connect(viewport_, &QObject::destroyed, this, [this] {
        viewport_ = nullptr;
        viewport_connections_.clear();
        refresh();
      }));
    }
  }
  refresh();
}

void DrawingManagerPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  refresh();
}

void DrawingManagerPanel::refresh() {
  const bool has_viewport = viewport_ != nullptr;
  add_button_->setEnabled(has_viewport);
  if (!has_viewport) {
    hint_->setText(tr("Open a model document to manage its drawings here."));
    pages_->setCurrentWidget(hint_);
    tree_->clear();
    sync_buttons();
    return;
  }
  sync_rows();
}

void DrawingManagerPanel::sync_rows() {
  const std::vector<DrawingRef>& drawings = viewport_->document().drawings();
  syncing_ = true;
  tree_->clear();
  for (const DrawingRef& drawing : drawings) {
    auto* item = new QTreeWidgetItem(tree_);
    apply_drawing_row(item, drawing);
  }
  syncing_ = false;

  const bool empty = drawings.empty();
  if (empty) {
    hint_->setText(tr("No drawings yet. Use “Add…” to attach DWF / DXF / PDF sheets to this "
                      "document — they show up under the model in the viewport."));
  }
  pages_->setCurrentWidget(empty ? static_cast<QWidget*>(hint_) : static_cast<QWidget*>(tree_));
  sync_show_all();
  sync_buttons();
}

// 一行 = 文档里的一张图纸：第一列的勾是"在视口里画不画"，第二列是状态
//（格式 / 文件缺失 / 读不了）。
void DrawingManagerPanel::apply_drawing_row(QTreeWidgetItem* item, const DrawingRef& drawing) {
  const QString file = QString::fromStdString(drawing.path);
  const QFileInfo info(file);
  const bool exists = info.exists();
  const QString error = viewport_ != nullptr ? viewport_->drawing_status(drawing.path) : QString();

  item->setText(kVisibleColumn, info.fileName());
  item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
  item->setCheckState(kVisibleColumn, drawing.visible ? Qt::Checked : Qt::Unchecked);
  item->setData(kVisibleColumn, kPathRole, file);
  QString status = exists ? info.suffix().toUpper() : tr("Missing");
  if (exists && !error.isEmpty()) {
    status = tr("Unreadable");
  }
  item->setText(kStatusColumn, status);
  item->setToolTip(kVisibleColumn, error.isEmpty() ? file : file + QLatin1Char('\n') + error);
  if (!exists || !error.isEmpty()) {
    const ThemePalette palette = theme_palette(dark_);
    item->setForeground(kVisibleColumn, QColor(palette.text_muted));
    item->setForeground(kStatusColumn, QColor(0xD9, 0x30, 0x25));
    item->setToolTip(kStatusColumn,
                     !error.isEmpty() ? error : tr("The file is not where it was when it was added"));
  }
}

void DrawingManagerPanel::sync_show_all() {
  if (show_all_ == nullptr || viewport_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(show_all_);
  show_all_->setChecked(viewport_->drawings_visible());
}

QString DrawingManagerPanel::selected_path() const {
  const QList<QTreeWidgetItem*> selected = tree_->selectedItems();
  if (selected.isEmpty()) {
    return {};
  }
  return selected.first()->data(kVisibleColumn, kPathRole).toString();
}

void DrawingManagerPanel::sync_buttons() {
  const bool has_selection = !selected_path().isEmpty();
  for (QToolButton* button : {open_button_, locate_button_, settings_button_, remove_button_}) {
    if (button != nullptr) {
      button->setEnabled(has_selection);
    }
  }
  show_all_->setEnabled(viewport_ != nullptr);
}

void DrawingManagerPanel::add_drawings() {
  if (viewport_ == nullptr) {
    return;
  }
  const QStringList files = QFileDialog::getOpenFileNames(
      this, tr("Add Drawings"), QString(), DrawingDocument::file_dialog_filter());
  if (files.isEmpty()) {
    return;
  }
  std::vector<std::string> paths;
  paths.reserve(static_cast<std::size_t>(files.size()));
  for (const QString& file : files) {
    paths.push_back(QFileInfo(file).absoluteFilePath().toStdString());
  }
  viewport_->add_document_drawings(paths);
  refresh();
}

void DrawingManagerPanel::open_selected_in_2d() {
  const QString path = selected_path();
  if (!path.isEmpty()) {
    emit open_requested(path);
  }
}

void DrawingManagerPanel::locate_selected() {
  const QString path = selected_path();
  if (viewport_ == nullptr || path.isEmpty()) {
    return;
  }
  // 定位 = 在三维视口里框住这张底图（不是另开页）；看不见就先把总开关打开。
  viewport_->set_drawings_visible(true);
  viewport_->frame_drawing(path.toStdString());
}

void DrawingManagerPanel::edit_selected() {
  const QString path = selected_path();
  if (viewport_ == nullptr || path.isEmpty() || syncing_) {
    return;
  }
  const DrawingRef* drawing = viewport_->document().drawing(path.toStdString());
  if (drawing == nullptr) {
    return;
  }
  const std::optional<DocumentViewport::DrawingInfo> info =
      viewport_->drawing_info(drawing->path);
  const int page_count = info.has_value() ? info->page_count : 1;
  const double unit_scale = info.has_value() ? info->declared_unit_scale : 0.0;
  DrawingSettingsDialog dialog(
      *drawing, page_count, unit_scale,
      [this, path](int page) {
        return viewport_->suggested_drawing_placement(path.toStdString(), page);
      },
      this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const std::string key = path.toStdString();
  viewport_->set_drawing_placement(key, dialog.placement(), dialog.page());
  viewport_->set_drawing_visible(key, dialog.visible_in_viewport());
  refresh();
}

void DrawingManagerPanel::remove_selected() {
  const QString path = selected_path();
  if (viewport_ == nullptr || path.isEmpty()) {
    return;
  }
  viewport_->remove_document_drawing(path.toStdString());
  refresh();
}

// 勾选框直接改文档里的显隐（随 .tdoc 存）。刷新走视口的 drawings_changed 信号，
// 队列投递——正在处理 itemChanged 的时候把整棵树 clear 掉会踩到 Qt 的迭代。
void DrawingManagerPanel::on_item_changed(QTreeWidgetItem* item, int column) {
  if (syncing_ || viewport_ == nullptr || item == nullptr || column != kVisibleColumn) {
    return;
  }
  const QString path = item->data(kVisibleColumn, kPathRole).toString();
  if (path.isEmpty()) {
    return;
  }
  viewport_->set_drawing_visible(path.toStdString(), item->checkState(kVisibleColumn) == Qt::Checked);
}

void DrawingManagerPanel::on_show_all_toggled(bool on) {
  if (viewport_ == nullptr) {
    return;
  }
  viewport_->set_drawings_visible(on);
}

void DrawingManagerPanel::on_item_double_clicked(QTreeWidgetItem* item, int column) {
  (void)column;
  if (syncing_ || item == nullptr || viewport_ == nullptr) {
    return;
  }
  const QString path = item->data(kVisibleColumn, kPathRole).toString();
  if (!path.isEmpty()) {
    // 双击 = 在视口里看这张底图（另开二维页走「2D page」按钮）。
    viewport_->set_drawings_visible(true);
    viewport_->frame_drawing(path.toStdString());
  }
}

}  // namespace tamias
