#include "floor_manager_panel.h"

#include "document_viewport.h"
#include "theme.h"

#include <QFont>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace tamias {
namespace {

constexpr int kFloorIndexRole = Qt::UserRole;  // 楼层下标；-1 = 全局三维
constexpr int kSignatureRole = Qt::UserRole + 1;  // 判"行要不要重建"的稳定身份
constexpr int kGlobalRow = -1;

QIcon tinted_mask_icon(const QString& resource, const QColor& color) {
  const QIcon source(resource);
  QIcon result;
  for (int size : {16, 18, 20, 32}) {
    const QPixmap src = source.pixmap(QSize(size, size));
    QPixmap tinted(src.size());
    tinted.setDevicePixelRatio(src.devicePixelRatio());
    tinted.fill(Qt::transparent);
    QPainter painter(&tinted);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawPixmap(0, 0, src);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();
    result.addPixmap(tinted);
  }
  return result;
}

QString manager_stylesheet(bool dark) {
  const ThemePalette palette = theme_palette(dark);
  QString sheet = QStringLiteral(
      "QWidget#floorManagerHeader { background: transparent;"
      "  border-bottom: 1px solid $divider; }"
      "QLabel#floorManagerCaption { color: $muted; }"
      "QTreeWidget#floorManagerTree { background: transparent; border: none; outline: none; }"
      "QTreeWidget#floorManagerTree::item { padding: 4px 2px; border-radius: 4px; }"
      "QTreeWidget#floorManagerTree::item:hover { background: $hover; }"
      "QTreeWidget#floorManagerTree::item:selected { background: $checked; }"
      "QTreeWidget#floorManagerTree QHeaderView::section {"
      "  background: transparent; color: $muted; border: none;"
      "  border-bottom: 1px solid $divider; padding: 2px 4px; font-size: 11px;"
      "}"
      "QLabel#floorManagerHint { color: $muted; padding: 16px 12px; }");
  sheet.replace(QStringLiteral("$divider"), css_color(palette.divider));
  sheet.replace(QStringLiteral("$muted"), css_color(palette.text_muted));
  sheet.replace(QStringLiteral("$hover"), css_color(palette.hover));
  sheet.replace(QStringLiteral("$checked"), css_color(palette.checked));
  return sheet;
}

QString floor_row_signature(const ViewportFloor& floor) {
  return QStringLiteral("%1|%2")
      .arg(static_cast<qulonglong>(floor.storey_id))
      .arg(QString::fromStdString(floor.label));
}

}  // namespace

FloorManagerPanel::FloorManagerPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("floorManagerPanel"));
  dark_ = is_dark_theme();

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* header = new QWidget(this);
  header->setObjectName(QStringLiteral("floorManagerHeader"));
  auto* bar = new QVBoxLayout(header);
  bar->setContentsMargins(10, 8, 10, 8);
  bar->setSpacing(2);
  auto* caption = new QLabel(tr("Views"), header);
  caption->setObjectName(QStringLiteral("floorManagerCaption"));
  bar->addWidget(caption);
  auto* sub = new QLabel(tr("Double-click a floor to open its view"), header);
  sub->setObjectName(QStringLiteral("floorManagerCaption"));
  sub->setWordWrap(true);
  bar->addWidget(sub);
  root->addWidget(header);

  pages_ = new QStackedWidget(this);

  tree_ = new QTreeWidget(pages_);
  tree_->setObjectName(QStringLiteral("floorManagerTree"));
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("View"), tr("Elevation")});
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  tree_->setColumnWidth(1, 68);
  tree_->setRootIsDecorated(false);
  tree_->setIconSize(QSize(16, 16));
  tree_->setUniformRowHeights(true);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  pages_->addWidget(tree_);

  hint_ = new QLabel(pages_);
  hint_->setObjectName(QStringLiteral("floorManagerHint"));
  hint_->setWordWrap(true);
  hint_->setAlignment(Qt::AlignCenter);
  pages_->addWidget(hint_);
  pages_->setCurrentWidget(hint_);
  root->addWidget(pages_, 1);

  connect(tree_, &QTreeWidget::itemDoubleClicked, this,
          &FloorManagerPanel::on_item_double_clicked);

  setStyleSheet(manager_stylesheet(dark_));
  refresh();
}

void FloorManagerPanel::set_dark_theme(bool dark) {
  if (dark_ == dark) {
    return;
  }
  dark_ = dark;
  setStyleSheet(manager_stylesheet(dark_));
  refresh();
}

void FloorManagerPanel::set_viewport(DocumentViewport* viewport) {
  if (viewport_ != viewport) {
    for (const QMetaObject::Connection& connection : viewport_connections_) {
      QObject::disconnect(connection);
    }
    viewport_connections_.clear();
    viewport_ = viewport;
    if (viewport_ != nullptr) {
      viewport_connections_.push_back(
          connect(viewport_, &DocumentViewport::view_changed, this, &FloorManagerPanel::refresh));
      viewport_connections_.push_back(connect(viewport_, &DocumentViewport::visibility_changed,
                                              this, &FloorManagerPanel::refresh));
      viewport_connections_.push_back(
          connect(viewport_, &DocumentViewport::document_changed, this, &FloorManagerPanel::refresh));
      viewport_connections_.push_back(connect(viewport_, &QObject::destroyed, this, [this] {
        viewport_ = nullptr;
        viewport_connections_.clear();
        refresh();
      }));
    }
  }
  refresh();
}

void FloorManagerPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  refresh();
}

void FloorManagerPanel::refresh() {
  const bool has_viewport = viewport_ != nullptr;
  if (!has_viewport) {
    hint_->setText(tr("Open a model document to open its floor views here."));
    pages_->setCurrentWidget(hint_);
    tree_->clear();
    return;
  }
  sync_rows();
}

void FloorManagerPanel::sync_rows() {
  const std::vector<ViewportFloor> floors = viewport_->floors();
  // 行只在楼层表变了以后重建（第 0 行恒为全局三维），否则原地刷新文字与高亮，
  // 避免在 itemDoubleClicked 里删掉正被用的项。
  bool rebuild = tree_->topLevelItemCount() != static_cast<int>(floors.size()) + 1;
  for (int i = 0; !rebuild && i + 1 < tree_->topLevelItemCount(); ++i) {
    const QTreeWidgetItem* item = tree_->topLevelItem(i + 1);
    rebuild = item->data(0, kSignatureRole).toString() !=
              floor_row_signature(floors[static_cast<std::size_t>(i)]);
  }

  const ThemePalette palette = theme_palette(dark_);
  const QIcon global_icon = tinted_mask_icon(QStringLiteral(":/icons/view_3d.svg"), palette.icon);
  const QIcon floor_icon = tinted_mask_icon(QStringLiteral(":/icons/storey.svg"), palette.icon);
  const QBrush active(palette.text);
  const QBrush muted(palette.text_muted);
  const QColor accent(47, 125, 222);

  syncing_ = true;
  if (rebuild) {
    tree_->clear();
    auto* global_item = new QTreeWidgetItem(tree_);
    global_item->setData(0, kFloorIndexRole, kGlobalRow);
    global_item->setData(0, kSignatureRole, QStringLiteral("global"));
    global_item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    for (std::size_t i = 0; i < floors.size(); ++i) {
      auto* item = new QTreeWidgetItem(tree_);
      item->setData(0, kFloorIndexRole, static_cast<int>(i));
      item->setData(0, kSignatureRole, floor_row_signature(floors[i]));
      item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
  }

  // 当前打开的是哪张视图：全局三维（默认），或者某一层。
  const bool floor_open = viewport_->floor_view_open();
  const int active_row =
      floor_open ? static_cast<int>(viewport_->floor_view_index()) + 1 : 0;

  QTreeWidgetItem* global_item = tree_->topLevelItem(0);
  global_item->setText(0, tr("Global 3D"));
  global_item->setText(1, QString());
  global_item->setIcon(0, global_icon);
  global_item->setToolTip(0, tr("Double-click to open the global 3D view"));

  for (std::size_t i = 0; i < floors.size(); ++i) {
    const ViewportFloor& floor = floors[i];
    QTreeWidgetItem* item = tree_->topLevelItem(static_cast<int>(i) + 1);
    QString label = QString::fromStdString(floor.label);
    if (floor.mezzanine) {
      label = tr("[Mezzanine] %1").arg(label);
    }
    item->setText(0, label);
    item->setText(1, QStringLiteral("%1 m").arg(static_cast<double>(floor.y_min), 0, 'f', 3));
    item->setIcon(0, floor_icon);
    item->setToolTip(0, tr("Double-click to open this floor's view"));
  }

  for (int row = 0; row < tree_->topLevelItemCount(); ++row) {
    QTreeWidgetItem* item = tree_->topLevelItem(row);
    const bool is_active = row == active_row;
    const bool mezzanine =
        row > 0 && floors[static_cast<std::size_t>(row) - 1].mezzanine;
    item->setForeground(0, is_active ? QBrush(accent) : active);
    item->setForeground(1, muted);
    QFont font = item->font(0);
    font.setBold(is_active);
    font.setItalic(mezzanine);
    item->setFont(0, font);
  }
  tree_->setCurrentItem(tree_->topLevelItem(active_row));
  syncing_ = false;

  const bool has_floors = !floors.empty();
  if (!has_floors) {
    hint_->setText(tr("This model has no floors yet. Use Floor Settings to add them."));
  }
  pages_->setCurrentWidget(has_floors ? static_cast<QWidget*>(tree_)
                                      : static_cast<QWidget*>(hint_));
}

void FloorManagerPanel::on_item_double_clicked(QTreeWidgetItem* item, int column) {
  (void)column;
  if (syncing_ || item == nullptr || viewport_ == nullptr) {
    return;
  }
  open_view_for(item);
}

void FloorManagerPanel::open_view_for(QTreeWidgetItem* item) {
  const int index = item->data(0, kFloorIndexRole).toInt();
  if (index == kGlobalRow) {
    viewport_->open_global_view();
    return;
  }
  viewport_->open_floor_view(static_cast<std::size_t>(index));
}

}  // namespace tamias
