#include "floor_panel.h"

#include "document_viewport.h"
#include "floor_settings_dialog.h"
#include "theme.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <cmath>

namespace tamias {
namespace {

constexpr int kFloorIndexRole = Qt::UserRole;
constexpr int kStoreyIdRole = Qt::UserRole + 1;
constexpr int kDerivedRole = Qt::UserRole + 2;
constexpr int kLabelRole = Qt::UserRole + 3;

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

QString floor_stylesheet(bool dark) {
  const ThemePalette palette = theme_palette(dark);
  const QColor accent = QColor(47, 125, 222);
  const QColor field_bg = dark ? QColor(30, 31, 34) : QColor(255, 255, 255);
  const QColor box_bg = dark ? QColor(30, 31, 34) : QColor(255, 255, 255);
  const QColor box_border = dark ? QColor(90, 96, 105) : QColor(169, 176, 184);
  QString sheet = QStringLiteral(
      "QWidget#floorHeader { background: transparent; border-bottom: 1px solid $divider; }"
      "QComboBox#floorStoreyCombo {"
      "  background: $field; color: $text; border: 1px solid $border;"
      "  border-radius: 4px; padding: 3px 6px;"
      "}"
      "QComboBox#floorStoreyCombo:disabled { color: $muted; }"
      "QComboBox#floorStoreyCombo QAbstractItemView {"
      "  background: $field; color: $text; selection-background-color: $accent;"
      "  border: 1px solid $border;"
      "}"
      "QToolButton#floorToolButton {"
      "  padding: 4px 8px; border-radius: 4px; border: none; color: $text;"
      "  background: transparent;"
      "}"
      "QToolButton#floorToolButton:hover { background: $hover; }"
      "QToolButton#floorToolButton:pressed { background: $checked; }"
      "QToolButton#floorToolButton:disabled { color: $muted; }"
      "QTreeWidget#floorTree { background: transparent; border: none; outline: none; }"
      "QTreeWidget#floorTree::item { padding: 3px 2px; border-radius: 4px; }"
      "QTreeWidget#floorTree::item:hover { background: $hover; }"
      "QTreeWidget#floorTree::item:selected { background: $checked; }"
      "QTreeWidget#floorTree QHeaderView::section {"
      "  background: transparent; color: $muted; border: none;"
      "  border-bottom: 1px solid $divider; padding: 2px 4px; font-size: 11px;"
      "}"
      "QTreeWidget#floorTree::indicator {"
      "  width: 14px; height: 14px; border-radius: 3px;"
      "  border: 1px solid $box_border; background: $box_bg;"
      "}"
      "QTreeWidget#floorTree::indicator:hover { border-color: $accent; }"
      "QTreeWidget#floorTree::indicator:checked {"
      "  border-color: $accent; background: $accent; image: url(:/icons/check.svg);"
      "}"
      "QLabel#floorHint { color: $muted; padding: 16px 12px; }");
  sheet.replace(QStringLiteral("$divider"), css_color(palette.divider));
  sheet.replace(QStringLiteral("$field"), css_color(field_bg));
  sheet.replace(QStringLiteral("$border"), css_color(palette.border));
  sheet.replace(QStringLiteral("$text"), css_color(palette.text));
  sheet.replace(QStringLiteral("$accent"), css_color(accent));
  sheet.replace(QStringLiteral("$muted"), css_color(palette.text_muted));
  sheet.replace(QStringLiteral("$hover"), css_color(palette.hover));
  sheet.replace(QStringLiteral("$checked"), css_color(palette.checked));
  sheet.replace(QStringLiteral("$box_border"), css_color(box_border));
  sheet.replace(QStringLiteral("$box_bg"), css_color(box_bg));
  return sheet;
}

}  // namespace

FloorPanel::FloorPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("floorPanel"));
  dark_ = is_dark_theme();

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* header = new QWidget(this);
  header->setObjectName(QStringLiteral("floorHeader"));
  auto* bar = new QHBoxLayout(header);
  bar->setContentsMargins(10, 8, 10, 8);
  bar->setSpacing(6);

  auto* caption = new QLabel(tr("Current floor"), header);
  caption->setStyleSheet(QStringLiteral("color: palette(mid);"));
  bar->addWidget(caption);

  storey_combo_ = new QComboBox(header);
  storey_combo_->setObjectName(QStringLiteral("floorStoreyCombo"));
  bar->addWidget(storey_combo_, 1);

  settings_ = new QToolButton(header);
  settings_->setObjectName(QStringLiteral("floorToolButton"));
  settings_->setAutoRaise(true);
  settings_->setCursor(Qt::PointingHandCursor);
  settings_->setFocusPolicy(Qt::NoFocus);
  settings_->setIcon(
      tinted_mask_icon(QStringLiteral(":/icons/settings.svg"), theme_palette(dark_).icon));
  settings_->setIconSize(QSize(16, 16));
  settings_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  settings_->setToolTip(tr("Floor Settings"));
  bar->addWidget(settings_);
  root->addWidget(header);

  pages_ = new QStackedWidget(this);

  tree_ = new QTreeWidget(pages_);
  tree_->setObjectName(QStringLiteral("floorTree"));
  tree_->setColumnCount(3);
  tree_->setHeaderLabels({tr("Floor"), tr("Elevation"), tr("Floor Height")});
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  tree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
  tree_->setColumnWidth(1, 62);
  tree_->setColumnWidth(2, 62);
  tree_->setRootIsDecorated(false);
  tree_->setIconSize(QSize(16, 16));
  tree_->setUniformRowHeights(true);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  pages_->addWidget(tree_);

  hint_ = new QLabel(pages_);
  hint_->setObjectName(QStringLiteral("floorHint"));
  hint_->setWordWrap(true);
  hint_->setAlignment(Qt::AlignCenter);
  pages_->addWidget(hint_);
  pages_->setCurrentWidget(hint_);
  root->addWidget(pages_, 1);

  auto* footer = new QWidget(this);
  footer->setObjectName(QStringLiteral("floorFooter"));
  auto* footer_bar = new QHBoxLayout(footer);
  footer_bar->setContentsMargins(10, 6, 10, 6);
  footer_bar->setSpacing(6);
  show_all_ = new QToolButton(footer);
  show_all_->setObjectName(QStringLiteral("floorToolButton"));
  show_all_->setAutoRaise(true);
  show_all_->setCursor(Qt::PointingHandCursor);
  show_all_->setFocusPolicy(Qt::NoFocus);
  show_all_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  show_all_->setText(tr("Show All"));
  show_all_->setToolTip(tr("Clear hidden, isolated and floor filters"));
  footer_bar->addWidget(show_all_);
  footer_bar->addStretch(1);
  root->addWidget(footer);

  connect(settings_, &QToolButton::clicked, this, &FloorPanel::open_floor_settings);
  connect(show_all_, &QToolButton::clicked, this, [this] {
    if (viewport_ != nullptr) {
      viewport_->show_all_visible();
    }
  });
  connect(tree_, &QTreeWidget::itemChanged, this, &FloorPanel::on_item_changed);
  connect(tree_, &QTreeWidget::itemClicked, this, &FloorPanel::on_item_clicked);
  connect(storey_combo_, &QComboBox::currentIndexChanged, this,
          &FloorPanel::on_storey_combo_changed);

  setStyleSheet(floor_stylesheet(dark_));
  refresh();
}

void FloorPanel::set_dark_theme(bool dark) {
  if (dark_ == dark) {
    return;
  }
  dark_ = dark;
  settings_->setIcon(
      tinted_mask_icon(QStringLiteral(":/icons/settings.svg"), theme_palette(dark_).icon));
  setStyleSheet(floor_stylesheet(dark_));
  refresh();
}

void FloorPanel::set_viewport(DocumentViewport* viewport) {
  if (viewport_ != viewport) {
    for (const QMetaObject::Connection& connection : viewport_connections_) {
      QObject::disconnect(connection);
    }
    viewport_connections_.clear();
    viewport_ = viewport;
    if (viewport_ != nullptr) {
      viewport_connections_.push_back(connect(viewport_, &DocumentViewport::visibility_changed,
                                              this, &FloorPanel::refresh));
      viewport_connections_.push_back(
          connect(viewport_, &DocumentViewport::document_changed, this, &FloorPanel::refresh));
      viewport_connections_.push_back(connect(viewport_, &QObject::destroyed, this, [this] {
        viewport_ = nullptr;
        viewport_connections_.clear();
        refresh();
      }));
    }
  }
  refresh();
}

void FloorPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  refresh();
}

void FloorPanel::refresh() {
  const bool has_viewport = viewport_ != nullptr;
  settings_->setEnabled(has_viewport);
  show_all_->setEnabled(has_viewport && viewport_->has_active_filter());
  storey_combo_->setEnabled(has_viewport);
  if (!has_viewport) {
    hint_->setText(tr("Open a model document to show or hide its floors here."));
    pages_->setCurrentWidget(hint_);
    tree_->clear();
    const QSignalBlocker blocker(storey_combo_);
    storey_combo_->clear();
    return;
  }
  sync_storey_combo();
  sync_rows();
}

void FloorPanel::sync_storey_combo() {
  const BimModel& bim = viewport_->document().bim();
  const QSignalBlocker blocker(storey_combo_);
  storey_combo_->clear();
  storey_combo_->addItem(tr("Unassigned"), static_cast<qulonglong>(0));
  int selected = 0;
  for (const Storey& storey : bim.storeys()) {
    storey_combo_->addItem(QString::fromStdString(storey.name),
                           static_cast<qulonglong>(storey.id));
    if (storey.id == bim.active_storey_id()) {
      selected = storey_combo_->count() - 1;
    }
  }
  storey_combo_->setCurrentIndex(selected);
}

void FloorPanel::sync_rows() {
  const std::vector<ViewportFloor> floors = viewport_->floors();
  // 行只在楼层表变了以后重建；否则原地改勾选。避免在 itemClicked 里删掉正被用的项。
  bool rebuild = tree_->topLevelItemCount() != static_cast<int>(floors.size());
  for (int i = 0; !rebuild && i < tree_->topLevelItemCount(); ++i) {
    const QTreeWidgetItem* item = tree_->topLevelItem(i);
    const ViewportFloor& floor = floors[static_cast<std::size_t>(i)];
    rebuild = item->data(0, kStoreyIdRole).toULongLong() != floor.storey_id ||
              item->data(0, kDerivedRole).toBool() != floor.derived ||
              item->data(0, kLabelRole).toString() != QString::fromStdString(floor.label);
  }

  const ThemePalette palette = theme_palette(dark_);
  const QBrush active(palette.text);
  const QBrush muted(palette.text_muted);
  syncing_ = true;
  if (rebuild) {
    tree_->clear();
    for (std::size_t i = 0; i < floors.size(); ++i) {
      auto* item = new QTreeWidgetItem(tree_);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(0, Qt::Checked);
      item->setData(0, kFloorIndexRole, static_cast<int>(i));
      item->setData(0, kStoreyIdRole, static_cast<qulonglong>(floors[i].storey_id));
      item->setData(0, kDerivedRole, floors[i].derived);
      item->setData(0, kLabelRole, QString::fromStdString(floors[i].label));
      item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
      item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    }
  }

  for (std::size_t i = 0; i < floors.size(); ++i) {
    const ViewportFloor& floor = floors[i];
    QTreeWidgetItem* item = tree_->topLevelItem(static_cast<int>(i));
    QString label = QString::fromStdString(floor.label);
    if (floor.mezzanine) {
      label = tr("[Mezzanine] %1").arg(label);
    }
    item->setText(0, label);
    item->setText(1, QStringLiteral("%1 m").arg(static_cast<double>(floor.y_min), 0, 'f', 3));
    item->setText(2, QStringLiteral("%1 m").arg(static_cast<double>(floor.height), 0, 'f', 3));
    item->setToolTip(0, floor.derived
                            ? tr("Inferred from geometry: this model has no floor records yet.")
                            : tr("Click to make this the current floor; untick to hide it."));
    const bool hidden = viewport_->floor_hidden(i);
    item->setCheckState(0, hidden ? Qt::Unchecked : Qt::Checked);
    item->setForeground(0, hidden ? muted : active);
    item->setForeground(1, muted);
    item->setForeground(2, muted);
    QFont font = item->font(0);
    font.setItalic(floor.mezzanine);
    item->setFont(0, font);
  }
  syncing_ = false;

  const bool has_floors = !floors.empty();
  if (!has_floors) {
    hint_->setText(tr("This model has no floors yet. Use Floor Settings to add them."));
  }
  pages_->setCurrentWidget(has_floors ? static_cast<QWidget*>(tree_)
                                      : static_cast<QWidget*>(hint_));
}

void FloorPanel::on_item_changed(QTreeWidgetItem* item, int column) {
  if (syncing_ || item == nullptr || column != 0 || viewport_ == nullptr) {
    return;
  }
  const int index = item->data(0, kFloorIndexRole).toInt();
  viewport_->set_floor_hidden(static_cast<std::size_t>(index),
                              item->checkState(0) != Qt::Checked);
}

void FloorPanel::on_item_clicked(QTreeWidgetItem* item, int column) {
  (void)column;
  if (syncing_ || item == nullptr || viewport_ == nullptr) {
    return;
  }
  const auto storey_id = static_cast<std::uint64_t>(item->data(0, kStoreyIdRole).toULongLong());
  if (storey_id == 0 || viewport_->document().bim().active_storey_id() == storey_id) {
    return;
  }
  viewport_->set_active_storey(storey_id);
}

void FloorPanel::on_storey_combo_changed(int index) {
  if (syncing_ || viewport_ == nullptr || index < 0) {
    return;
  }
  const auto storey_id =
      static_cast<std::uint64_t>(storey_combo_->itemData(index).toULongLong());
  if (viewport_->document().bim().active_storey_id() == storey_id) {
    return;
  }
  viewport_->set_active_storey(storey_id);
}

void FloorPanel::open_floor_settings() {
  if (viewport_ == nullptr) {
    return;
  }
  const BimModel& bim = viewport_->document().bim();
  const std::vector<Storey> before = bim.storeys();
  const std::uint64_t active = bim.active_storey_id();

  FloorSettingsDialog dialog(before, active, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const std::vector<Storey>& plan = dialog.storeys();
  const std::uint64_t next_active = dialog.active_storey_id();
  bool same = plan.size() == before.size() && next_active == active;
  for (std::size_t i = 0; same && i < plan.size(); ++i) {
    const Storey* old = bim.find_storey(plan[i].id);
    same = old != nullptr && old->name == plan[i].name &&
           std::abs(old->elevation - plan[i].elevation) <= 1e-9 &&
           std::abs(old->height - plan[i].height) <= 1e-9 &&
           old->mezzanine == plan[i].mezzanine;
  }
  if (same) {
    return;  // 没改就不往撤销栈里塞空命令
  }
  viewport_->apply_storey_settings(plan, next_active);
}

}  // namespace tamias
