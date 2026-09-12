#include "visibility_panel.h"

#include "document_viewport.h"
#include "entity_kind_catalog.h"
#include "theme.h"

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <unordered_set>

namespace tamias {
namespace {

constexpr int kRowTypeRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kCountRole = Qt::UserRole + 2;

QString panel_stylesheet(bool dark) {
  const ThemePalette palette = theme_palette(dark);
  const QColor field_bg = dark ? QColor(30, 31, 34) : QColor(255, 255, 255);
  const QColor accent = QColor(47, 125, 222);
  // 复选框自己画：行样式一旦生效，Qt 就不再按系统样子画这个格子，
  // 浅色下会出现"框线和对号都看不清"。
  const QColor box_bg = dark ? QColor(30, 31, 34) : QColor(255, 255, 255);
  const QColor box_border = dark ? QColor(90, 96, 105) : QColor(169, 176, 184);
  QString sheet = QStringLiteral(
      "QWidget#visibilityHeader { background: transparent; border-bottom: 1px solid $divider; }"
      "QLineEdit#visibilitySearch {"
      "  background: $field; border: 1px solid $border; border-radius: 4px;"
      "  padding: 4px 8px; color: $text; selection-background-color: $accent;"
      "}"
      "QLineEdit#visibilitySearch:focus { border-color: $accent; }"
      "QLineEdit#visibilitySearch:disabled { color: $muted; }"
      "QToolButton#visibilityShowAll {"
      "  padding: 4px 10px; border-radius: 4px; border: none;"
      "  color: $text; background: transparent;"
      "}"
      "QToolButton#visibilityShowAll:hover { background: $hover; }"
      "QToolButton#visibilityShowAll:pressed { background: $checked; }"
      "QToolButton#visibilityShowAll:disabled { color: $muted; }"
      "QTreeWidget#visibilityTree { background: transparent; border: none; outline: none; }"
      "QTreeWidget#visibilityTree::item { padding: 3px 2px; border-radius: 4px; }"
      "QTreeWidget#visibilityTree::item:hover { background: $hover; }"
      "QTreeWidget#visibilityTree::item:selected { background: $checked; }"
      "QTreeWidget#visibilityTree::indicator {"
      "  width: 14px; height: 14px; border-radius: 3px;"
      "  border: 1px solid $box_border; background: $box_bg;"
      "}"
      "QTreeWidget#visibilityTree::indicator:hover { border-color: $accent; }"
      "QTreeWidget#visibilityTree::indicator:checked,"
      "QTreeWidget#visibilityTree::indicator:indeterminate {"
      "  border-color: $accent; background: $accent; image: url(:/icons/check.svg);"
      "}"
      "QTreeWidget#visibilityTree::indicator:indeterminate {"
      "  image: url(:/icons/check_partial.svg);"
      "}"
      "QLabel#visibilityHint { color: $muted; padding: 18px; }");
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

VisibilityPanel::VisibilityPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("visibilityPanel"));
  dark_ = is_dark_theme();

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* header = new QWidget(this);
  header->setObjectName(QStringLiteral("visibilityHeader"));
  auto* bar = new QHBoxLayout(header);
  bar->setContentsMargins(10, 8, 10, 8);
  bar->setSpacing(8);

  search_ = new QLineEdit(header);
  search_->setObjectName(QStringLiteral("visibilitySearch"));
  search_->setPlaceholderText(tr("Search components"));
  search_->setClearButtonEnabled(true);
  bar->addWidget(search_, 1);

  show_all_ = new QToolButton(header);
  show_all_->setObjectName(QStringLiteral("visibilityShowAll"));
  show_all_->setAutoRaise(true);
  show_all_->setCursor(Qt::PointingHandCursor);
  show_all_->setFocusPolicy(Qt::NoFocus);
  show_all_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  show_all_->setText(tr("Show All"));
  show_all_->setToolTip(tr("Clear hidden, isolated and floor filters"));
  bar->addWidget(show_all_);
  root->addWidget(header);

  pages_ = new QStackedWidget(this);

  tree_ = new QTreeWidget(pages_);
  tree_->setObjectName(QStringLiteral("visibilityTree"));
  tree_->setColumnCount(2);
  tree_->setHeaderHidden(true);
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
  tree_->setColumnWidth(1, 44);
  tree_->setRootIsDecorated(true);
  tree_->setIndentation(14);
  tree_->setIconSize(QSize(16, 16));
  tree_->setUniformRowHeights(true);
  tree_->setAnimated(true);
  tree_->setExpandsOnDoubleClick(false);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  pages_->addWidget(tree_);

  hint_ = new QLabel(tr("Open a model document to show or hide its components here."), pages_);
  hint_->setObjectName(QStringLiteral("visibilityHint"));
  hint_->setWordWrap(true);
  hint_->setAlignment(Qt::AlignCenter);
  pages_->addWidget(hint_);
  pages_->setCurrentWidget(hint_);
  root->addWidget(pages_, 1);

  build_tree();

  connect(search_, &QLineEdit::textChanged, this, &VisibilityPanel::apply_search);
  connect(show_all_, &QToolButton::clicked, this, [this] {
    if (viewport_ != nullptr) {
      viewport_->show_all_visible();
    }
  });
  connect(tree_, &QTreeWidget::itemChanged, this, &VisibilityPanel::on_item_changed);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this, &VisibilityPanel::on_item_double_clicked);
  connect(tree_, &QTreeWidget::customContextMenuRequested, this,
          &VisibilityPanel::show_context_menu);

  setStyleSheet(panel_stylesheet(dark_));
  refresh();
}

void VisibilityPanel::set_dark_theme(bool dark) {
  if (dark_ == dark) {
    return;
  }
  dark_ = dark;
  setStyleSheet(panel_stylesheet(dark_));
  refresh();
}

void VisibilityPanel::build_tree() {
  syncing_ = true;
  tree_->clear();
  kind_items_.clear();
  group_items_.clear();
  imported_item_ = nullptr;

  for (const Discipline discipline :
       {Discipline::Architectural, Discipline::Structural, Discipline::None}) {
    auto* group = new QTreeWidgetItem(tree_);
    group->setText(0, discipline_label(discipline));
    group->setFlags(group->flags() | Qt::ItemIsUserCheckable);
    group->setCheckState(0, Qt::Checked);
    group->setData(0, kRowTypeRole, static_cast<int>(RowType::Group));
    group->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    QFont font = group->font(0);
    font.setBold(true);
    group->setFont(0, font);
    group->setExpanded(true);
    group_items_.emplace_back(discipline, group);
  }
  const auto group_for = [this](Discipline discipline) {
    for (const auto& [value, item] : group_items_) {
      if (value == discipline) {
        return item;
      }
    }
    return group_items_.front().second;
  };

  for (const EntityKindEntry& entry : entity_kind_catalog()) {
    auto* item = new QTreeWidgetItem(group_for(entity_kind_discipline(entry.kind)));
    item->setText(0, entity_kind_label(entry.kind));
    item->setIcon(0, QIcon(QString::fromUtf8(entry.icon)));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, Qt::Checked);
    item->setData(0, kRowTypeRole, static_cast<int>(RowType::Kind));
    item->setData(0, kKindRole, static_cast<int>(entry.kind));
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    kind_items_.emplace(entry.kind, item);
  }

  // 导入网格（STEP / OBJ / glTF）没有 Entity，谈不上"类别"，只能整组显隐。
  imported_item_ = new QTreeWidgetItem(tree_);
  imported_item_->setText(0, tr("Imported meshes"));
  imported_item_->setIcon(0, QIcon(QStringLiteral(":/icons/drawing.svg")));
  imported_item_->setFlags(imported_item_->flags() | Qt::ItemIsUserCheckable);
  imported_item_->setCheckState(0, Qt::Checked);
  imported_item_->setData(0, kRowTypeRole, static_cast<int>(RowType::Imported));
  imported_item_->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);

  syncing_ = false;
}

void VisibilityPanel::set_viewport(DocumentViewport* viewport) {
  if (viewport_ != viewport) {
    for (const QMetaObject::Connection& connection : viewport_connections_) {
      QObject::disconnect(connection);
    }
    viewport_connections_.clear();
    viewport_ = viewport;
    if (viewport_ != nullptr) {
      viewport_connections_.push_back(connect(viewport_, &DocumentViewport::visibility_changed,
                                              this, &VisibilityPanel::refresh));
      viewport_connections_.push_back(connect(viewport_, &DocumentViewport::document_changed, this,
                                              &VisibilityPanel::refresh));
      viewport_connections_.push_back(connect(viewport_, &QObject::destroyed, this, [this] {
        viewport_ = nullptr;
        viewport_connections_.clear();
        refresh();
      }));
    }
  }
  refresh();
}

void VisibilityPanel::refresh() {
  const bool has_viewport = viewport_ != nullptr;
  search_->setEnabled(has_viewport);
  show_all_->setEnabled(has_viewport && viewport_->has_active_filter());
  if (!has_viewport) {
    hint_->setText(tr("Open a model document to show or hide its components here."));
    pages_->setCurrentWidget(hint_);
    return;
  }
  sync_rows();
  apply_search(search_->text());
  if (pages_->currentWidget() == hint_) {
    // 空文档和"搜不到"要分开说，否则看着像面板坏了。
    hint_->setText(search_->text().trimmed().isEmpty() ? tr("This document has no components yet.")
                                                       : tr("No component matches the search."));
  }
}

void VisibilityPanel::sync_rows() {
  const ThemePalette palette = theme_palette(dark_);
  const QBrush active(palette.text);
  const QBrush muted(palette.text_muted);
  const VisibilityCounts counts = viewport_->visibility_counts();
  const auto& hidden_kinds = viewport_->hidden_kinds();
  // 逐件隔离（视口右键"隔离"）没法用类别复选框表达：把被隔离挡住的类别
  // 直接显示成未勾选，面板就不会出现"勾着却看不见"。
  const bool isolating = viewport_->is_isolating();
  const std::unordered_set<EntityKind> isolated_kinds =
      isolating ? viewport_->isolated_kinds() : std::unordered_set<EntityKind>{};

  syncing_ = true;
  for (auto& [kind, item] : kind_items_) {
    const auto found = counts.kinds.find(kind);
    const int count = found == counts.kinds.end() ? 0 : static_cast<int>(found->second);
    const bool hidden =
        hidden_kinds.count(kind) != 0 || (isolating && isolated_kinds.count(kind) == 0);
    item->setData(0, kCountRole, count);
    item->setText(1, count > 0 ? QString::number(count) : QString());
    item->setCheckState(0, hidden ? Qt::Unchecked : Qt::Checked);
    // 隐藏的类别整行变灰，不打开面板也能从行色看出"现在藏了什么"。
    item->setForeground(0, hidden ? muted : active);
    item->setForeground(1, muted);
  }

  const int imported = static_cast<int>(counts.imported);
  const bool imported_hidden = viewport_->imported_hidden() || isolating;
  imported_item_->setData(0, kCountRole, imported);
  imported_item_->setText(1, imported > 0 ? QString::number(imported) : QString());
  imported_item_->setCheckState(0, imported_hidden ? Qt::Unchecked : Qt::Checked);
  imported_item_->setForeground(0, imported_hidden ? muted : active);
  imported_item_->setForeground(1, muted);
  syncing_ = false;
}

void VisibilityPanel::apply_search(const QString& text) {
  const QString needle = text.trimmed();
  const auto matches = [&needle](const QTreeWidgetItem* item) {
    return needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive);
  };

  for (auto& [kind, item] : kind_items_) {
    (void)kind;
    const bool present = item->data(0, kCountRole).toInt() > 0;
    set_row_hidden(item, !present || !matches(item));
  }
  if (imported_item_ != nullptr) {
    const bool present = imported_item_->data(0, kCountRole).toInt() > 0;
    set_row_hidden(imported_item_, !present || !matches(imported_item_));
  }
  update_groups();
}

void VisibilityPanel::update_groups() {
  syncing_ = true;
  bool any_visible = false;
  for (auto& [discipline, group] : group_items_) {
    (void)discipline;
    int total = 0;
    int checked = 0;
    int unchecked = 0;
    for (int i = 0; i < group->childCount(); ++i) {
      QTreeWidgetItem* child = group->child(i);
      if (child->isHidden()) {
        continue;
      }
      total += child->data(0, kCountRole).toInt();
      if (child->checkState(0) == Qt::Checked) {
        ++checked;
      } else {
        ++unchecked;
      }
    }
    const bool present = checked + unchecked > 0;
    group->setData(0, kCountRole, total);
    group->setText(1, present ? QString::number(total) : QString());
    group->setHidden(!present);
    group->setCheckState(0, unchecked == 0 ? Qt::Checked
                                           : (checked == 0 ? Qt::Unchecked : Qt::PartiallyChecked));
    if (present) {
      any_visible = true;
      if (!search_->text().trimmed().isEmpty()) {
        group->setExpanded(true);
      }
    }
  }
  if (imported_item_ != nullptr && !imported_item_->isHidden()) {
    any_visible = true;
  }
  syncing_ = false;
  pages_->setCurrentWidget(any_visible ? static_cast<QWidget*>(tree_)
                                       : static_cast<QWidget*>(hint_));
}

void VisibilityPanel::set_row_hidden(QTreeWidgetItem* item, bool hidden_row) {
  if (item == nullptr || item->isHidden() == hidden_row) {
    return;
  }
  const QSignalBlocker blocker(tree_);
  item->setHidden(hidden_row);
}

VisibilityPanel::RowType VisibilityPanel::row_type(const QTreeWidgetItem* item) {
  return static_cast<RowType>(item->data(0, kRowTypeRole).toInt());
}

bool VisibilityPanel::kind_of(const QTreeWidgetItem* item, EntityKind& out) {
  if (item == nullptr || row_type(item) != RowType::Kind) {
    return false;
  }
  out = static_cast<EntityKind>(item->data(0, kKindRole).toInt());
  return true;
}

void VisibilityPanel::on_item_changed(QTreeWidgetItem* item, int column) {
  if (syncing_ || viewport_ == nullptr || item == nullptr || column != 0 ||
      (item->flags() & Qt::ItemIsUserCheckable) == 0) {
    return;
  }
  const bool visible = item->checkState(0) == Qt::Checked;
  switch (row_type(item)) {
    case RowType::Group: {
      // 整组勾选：一次写回视口，避免逐个类别重绘。
      std::vector<EntityKind> kinds;
      kinds.reserve(static_cast<std::size_t>(item->childCount()));
      syncing_ = true;
      for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        if (child->isHidden()) {
          continue;
        }
        child->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        EntityKind kind = EntityKind::Wall;
        if (kind_of(child, kind)) {
          kinds.push_back(kind);
        }
      }
      syncing_ = false;
      viewport_->set_kinds_hidden(kinds, !visible);
      refresh();  // 视口没有变化时也要回填行色与父节点三态
      break;
    }
    case RowType::Kind: {
      EntityKind kind = EntityKind::Wall;
      if (kind_of(item, kind)) {
        viewport_->set_kind_hidden(kind, !visible);
      }
      break;
    }
    case RowType::Imported:
      viewport_->set_imported_hidden(!visible);
      break;
  }
}

void VisibilityPanel::on_item_double_clicked(QTreeWidgetItem* item, int column) {
  (void)column;
  if (item == nullptr || viewport_ == nullptr) {
    return;
  }
  if (row_type(item) == RowType::Group) {
    item->setExpanded(!item->isExpanded());
    return;
  }
  EntityKind kind = EntityKind::Wall;
  if (kind_of(item, kind)) {
    viewport_->frame_kind(kind);
  }
}

void VisibilityPanel::show_context_menu(const QPoint& pos) {
  QTreeWidgetItem* item = tree_->itemAt(pos);
  EntityKind kind = EntityKind::Wall;
  if (viewport_ == nullptr || !kind_of(item, kind)) {
    return;
  }
  QMenu menu(this);
  QAction* isolate = menu.addAction(tr("Show Only This Category"));
  QAction* frame = menu.addAction(tr("Frame This Category"));
  QAction* chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));
  if (chosen == isolate) {
    viewport_->isolate_kind(kind);
  } else if (chosen == frame) {
    viewport_->frame_kind(kind);
  }
}

void VisibilityPanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  refresh();
}

}  // namespace tamias
