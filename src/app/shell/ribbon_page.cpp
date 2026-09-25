#include "app/shell/ribbon_page.h"

#include "app/shell/ribbon_group.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace tamias {
namespace {

constexpr int kRowHeightText = 92;
constexpr int kRowHeightIconOnly = 54;
constexpr int kEdgeMargin = 4;

}  // namespace

RibbonPage::RibbonPage(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("ribbonPage"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("ribbonPageScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFocusPolicy(Qt::NoFocus);

  content_ = new QWidget(scroll);
  content_->setObjectName(QStringLiteral("ribbonPageContent"));
  auto* rows = new QVBoxLayout(content_);
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(0);
  for (int row = 0; row < kMaxRows; ++row) {
    auto* host = new QWidget(content_);
    host->setObjectName(QStringLiteral("ribbonPageRow"));
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(kEdgeMargin, 0, 2 * kEdgeMargin, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addStretch(1);
    rows->addWidget(host);
    row_hosts_[row] = host;
    row_layouts_[row] = layout;
  }
  // 第二排平时不占地方：拖到那儿、或者已经有组在第二排，才亮出来。
  row_hosts_[1]->hide();
  scroll->setWidget(content_);

  root->addWidget(scroll);
  apply_rows();
}

RibbonGroup* RibbonPage::add_group(const QString& title) {
  return add_group(title.trimmed().toCaseFolded(), title);
}

RibbonGroup* RibbonPage::add_group(const QString& id, const QString& title) {
  if (RibbonGroup* existing = find_group(id)) {
    return existing;
  }

  auto* group = new RibbonGroup(title, row_hosts_[0]);
  group->set_identity(page_id_, id);
  group->set_separator_visible(false);
  row_layouts_[0]->insertWidget(row_layouts_[0]->count() - 1, group);
  groups_by_id_.insert(id, group);
  apply_rows();
  emit group_added(group);
  return group;
}

RibbonGroup* RibbonPage::find_group(const QString& id) const {
  return groups_by_id_.value(id);
}

void RibbonPage::set_display_mode(RibbonDisplayMode mode) {
  row_height_ = mode == RibbonDisplayMode::IconOnly ? kRowHeightIconOnly : kRowHeightText;
  // 包括已经被拖出去的组：它们回来时也得是新样式。
  for (RibbonGroup* group : groups_by_id_) {
    if (group != nullptr) {
      group->set_display_mode(mode);
    }
  }
  apply_rows();
}

void RibbonPage::set_drop_target_visible(bool visible) {
  if (drop_target_visible_ == visible) {
    return;
  }
  drop_target_visible_ = visible;
  apply_rows();
}

// 空排收起；页高 = 可见排数 × 单排高度。
void RibbonPage::apply_rows() {
  const bool second = drop_target_visible_ || !ordered_groups(1).empty();
  for (int row = 0; row < kMaxRows; ++row) {
    // 每排等高：落点要按 y 分排，行高就必须是定数，不能由内容撑。
    row_hosts_[row]->setFixedHeight(row_height_);
    row_hosts_[row]->setVisible(row == 0 || second);
  }
  setFixedHeight((second ? 2 : 1) * row_height_);
  refresh_chrome();
}

RibbonPage::Slot RibbonPage::detach_group(RibbonGroup* group) {
  const int row = row_of(group);
  if (row < 0) {
    return {};
  }
  const std::vector<RibbonGroup*> groups = ordered_groups(row);
  int index = 0;
  for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
    if (groups[i] == group) {
      index = i;
      break;
    }
  }
  row_layouts_[row]->removeWidget(group);
  apply_rows();
  return Slot{row, index};
}

void RibbonPage::insert_group(RibbonGroup* group, Slot slot) {
  if (group == nullptr) {
    return;
  }
  // 已经在这一页里（拖动排序）：先摘掉，免得同一个控件在布局里出现两次。
  if (const int current = row_of(group); current >= 0) {
    row_layouts_[current]->removeWidget(group);
  }
  const int row = std::clamp(slot.row, 0, kMaxRows - 1);
  const std::vector<RibbonGroup*> groups = ordered_groups(row);
  const int clamped = std::clamp(slot.index, 0, static_cast<int>(groups.size()));
  if (clamped >= static_cast<int>(groups.size())) {
    // 末尾：插在最后的 stretch 之前。
    row_layouts_[row]->insertWidget(row_layouts_[row]->count() - 1, group);
  } else {
    row_layouts_[row]->insertWidget(row_layouts_[row]->indexOf(groups[clamped]), group);
  }
  group->setParent(row_hosts_[row]);
  group->show();
  apply_rows();
}

int RibbonPage::group_count() const {
  int total = 0;
  for (int row = 0; row < kMaxRows; ++row) {
    total += static_cast<int>(ordered_groups(row).size());
  }
  return total;
}

RibbonPage::Slot RibbonPage::drop_slot_at(const QPoint& content_pos) const {
  // 每排等高，所以排号就是 y 除以排高——不去读还没更新完的几何。
  // 第二排亮着时，压过一排高度就算想去第二排（往下拖出 Ribbon 也照样命中）。
  const int row = (row_hosts_[1]->isVisible() && content_pos.y() >= row_height_) ? 1 : 0;
  const std::vector<RibbonGroup*> groups = ordered_groups(row);
  for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
    if (content_pos.x() < groups[i]->geometry().center().x()) {
      return Slot{row, i};
    }
  }
  return Slot{row, static_cast<int>(groups.size())};
}

int RibbonPage::row_of(RibbonGroup* group) const {
  if (group == nullptr) {
    return -1;
  }
  for (int row = 0; row < kMaxRows; ++row) {
    if (row_layouts_[row]->indexOf(group) >= 0) {
      return row;
    }
  }
  return -1;
}

std::vector<RibbonGroup*> RibbonPage::ordered_groups(int row) const {
  std::vector<RibbonGroup*> groups;
  if (row < 0 || row >= kMaxRows) {
    return groups;
  }
  QHBoxLayout* layout = row_layouts_[row];
  for (int i = 0; i < layout->count(); ++i) {
    if (auto* group = qobject_cast<RibbonGroup*>(layout->itemAt(i)->widget())) {
      groups.push_back(group);
    }
  }
  return groups;
}

QFrame* RibbonPage::ensure_drop_indicator() {
  if (drop_indicator_ == nullptr) {
    drop_indicator_ = new QFrame(content_);
    drop_indicator_->setObjectName(QStringLiteral("ribbonDropIndicator"));
    drop_indicator_->setAttribute(Qt::WA_StyledBackground, true);
    drop_indicator_->setFrameShape(QFrame::NoFrame);
    drop_indicator_->hide();
  }
  return drop_indicator_;
}

void RibbonPage::show_drop_indicator(Slot slot) {
  QFrame* indicator = ensure_drop_indicator();
  const int row = std::clamp(slot.row, 0, kMaxRows - 1);
  QWidget* host = row_hosts_[row];
  const std::vector<RibbonGroup*> groups = ordered_groups(row);
  int x = host->geometry().left() + kEdgeMargin;
  if (!groups.empty()) {
    if (slot.index <= 0) {
      x = host->geometry().left() + groups.front()->geometry().left() - 3;
    } else if (slot.index >= static_cast<int>(groups.size())) {
      x = host->geometry().left() + groups.back()->geometry().right() + 1;
    } else {
      x = host->geometry().left() + groups[slot.index]->geometry().left() - 3;
    }
  }
  const int height = host->height() > 0 ? host->height() : row_height_;
  indicator->setGeometry(x, host->geometry().top() + 4, 2, (std::max)(8, height - 8));
  indicator->show();
  indicator->raise();
}

void RibbonPage::hide_drop_indicator() {
  if (drop_indicator_ != nullptr) {
    drop_indicator_->hide();
  }
}

void RibbonPage::refresh_chrome() {
  // 分隔线：每一排的最后一组不画（它是那一排的右边界）。
  for (int row = 0; row < kMaxRows; ++row) {
    const std::vector<RibbonGroup*> groups = ordered_groups(row);
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
      groups[i]->set_separator_visible(i + 1 != static_cast<int>(groups.size()));
    }
  }
}

}  // namespace tamias
