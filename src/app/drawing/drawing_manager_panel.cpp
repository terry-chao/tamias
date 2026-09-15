#include "app/drawing/drawing_manager_panel.h"

#include "app/viewport/canvas/document_viewport.h"
#include "app/drawing/drawing_document.h"
#include "engine/document/document.h"
#include "app/base/theme.h"

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
  auto* sub = new QLabel(tr("Double-click a drawing to open it"), header);
  sub->setObjectName(QStringLiteral("drawingCaption"));
  sub->setWordWrap(true);
  header_layout->addWidget(sub);

  auto* bar = new QHBoxLayout();
  bar->setContentsMargins(0, 4, 0, 0);
  bar->setSpacing(4);
  const auto make_button = [&](const QString& text, const QString& tip) {
    auto* button = new QToolButton(header);
    button->setObjectName(QStringLiteral("drawingToolButton"));
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addWidget(button);
    return button;
  };
  add_button_ = make_button(
      tr("Add…"), tr("Attach DWF / DWFx / DXF / PDF / SVG / image drawings to this document; "
                     "the files stay where they are"));
  open_button_ = make_button(tr("Open"), tr("Open the selected drawing in a 2D page"));
  remove_button_ = make_button(tr("Delete"), tr("Unlink the selected drawing (the file is kept)"));
  bar->addStretch(1);
  header_layout->addLayout(bar);
  root->addWidget(header);

  pages_ = new QStackedWidget(this);

  tree_ = new QTreeWidget(pages_);
  tree_->setObjectName(QStringLiteral("drawingTree"));
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("Drawing"), tr("Status")});
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  tree_->setColumnWidth(1, 72);
  tree_->setRootIsDecorated(false);
  tree_->setUniformRowHeights(true);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setToolTip(tr("Double-click a drawing to open it"));
  pages_->addWidget(tree_);

  hint_ = new QLabel(pages_);
  hint_->setObjectName(QStringLiteral("drawingHint"));
  hint_->setWordWrap(true);
  hint_->setAlignment(Qt::AlignCenter);
  pages_->addWidget(hint_);
  pages_->setCurrentWidget(hint_);
  root->addWidget(pages_, 1);

  connect(add_button_, &QToolButton::clicked, this, &DrawingManagerPanel::add_drawings);
  connect(open_button_, &QToolButton::clicked, this, &DrawingManagerPanel::open_selected);
  connect(remove_button_, &QToolButton::clicked, this, &DrawingManagerPanel::remove_selected);
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
      viewport_connections_.push_back(connect(viewport_, &DocumentViewport::document_changed,
                                              this, &DrawingManagerPanel::refresh));
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
  const std::vector<std::string>& paths = viewport_->document().drawing_paths();

  syncing_ = true;
  tree_->clear();
  for (const std::string& path : paths) {
    const QString file = QString::fromStdString(path);
    const QFileInfo info(file);
    const bool exists = info.exists();
    auto* item = new QTreeWidgetItem(tree_);
    item->setText(0, info.fileName());
    item->setText(1, exists ? info.suffix().toUpper() : tr("Missing"));
    item->setData(0, kPathRole, file);
    item->setToolTip(0, file);
    if (!exists) {
      const ThemePalette palette = theme_palette(dark_);
      item->setForeground(0, QColor(palette.text_muted));
      item->setForeground(1, QColor(0xD9, 0x30, 0x25));
      item->setToolTip(1, tr("The file is not where it was when it was added"));
    }
  }
  syncing_ = false;

  const bool empty = paths.empty();
  if (empty) {
    hint_->setText(tr("No drawings yet. Use “Add…” to attach DWF / DXF / PDF sheets to this "
                      "document."));
  }
  pages_->setCurrentWidget(empty ? static_cast<QWidget*>(hint_) : static_cast<QWidget*>(tree_));
  sync_buttons();
}

QString DrawingManagerPanel::selected_path() const {
  const QList<QTreeWidgetItem*> selected = tree_->selectedItems();
  if (selected.isEmpty()) {
    return {};
  }
  return selected.first()->data(0, kPathRole).toString();
}

void DrawingManagerPanel::sync_buttons() {
  const bool has_selection = !selected_path().isEmpty();
  if (open_button_ != nullptr) {
    open_button_->setEnabled(has_selection);
  }
  if (remove_button_ != nullptr) {
    remove_button_->setEnabled(has_selection);
  }
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

void DrawingManagerPanel::open_selected() {
  const QString path = selected_path();
  if (!path.isEmpty()) {
    emit open_requested(path);
  }
}

void DrawingManagerPanel::remove_selected() {
  const QString path = selected_path();
  if (viewport_ == nullptr || path.isEmpty()) {
    return;
  }
  viewport_->remove_document_drawing(path.toStdString());
  refresh();
}

void DrawingManagerPanel::on_item_double_clicked(QTreeWidgetItem* item, int column) {
  (void)column;
  if (syncing_ || item == nullptr || viewport_ == nullptr) {
    return;
  }
  const QString path = item->data(0, kPathRole).toString();
  if (!path.isEmpty()) {
    emit open_requested(path);
  }
}

}  // namespace tamias
