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
  rows_layout_ = new QVBoxLayout(content_);
  rows_layout_->setContentsMargins(0, 0, 0, 0);
  rows_layout_->setSpacing(0);
  ensure_rows(1);
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
  ensure_rows(1);
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
  drop_target_visible_ = visible;
  apply_rows();  // 不早退：状态一样也重算一次，免得某条路径把它落成"看着是空的但还占着"
}

QSize RibbonPage::sizeHint() const {
  QSize hint = QWidget::sizeHint();
  hint.setHeight(visible_rows_ * row_height_);
  return hint;
}

QSize RibbonPage::minimumSizeHint() const {
  QSize hint = QWidget::minimumSizeHint();
  hint.setHeight(visible_rows_ * row_height_);
  return hint;
}

void RibbonPage::ensure_rows(int count) {
  const int wanted = std::clamp(count, 1, kMaxRows);
  while (static_cast<int>(row_hosts_.size()) < wanted) {
    auto* host = new QWidget(content_);
    host->setObjectName(QStringLiteral("ribbonPageRow"));
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(kEdgeMargin, 0, 2 * kEdgeMargin, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addStretch(1);
    rows_layout_->addWidget(host);
    row_hosts_.push_back(host);
    row_layouts_.push_back(layout);
  }
}

int RibbonPage::rows_with_content() const {
  int last = 0;
  for (int row = 0; row < static_cast<int>(row_layouts_.size()); ++row) {
    if (!ordered_groups(row).empty()) {
      last = row;
    }
  }
  return last + 1;
}

// 空排只可能来自「分组被拖出去浮动」或「从中排拖到更后面的排」。
// 排是有序的容器，中间空一格没有意义——把后面的排依次往上挪。
void RibbonPage::compact_rows() {
  int write = 0;
  const int rows = static_cast<int>(row_layouts_.size());
  for (int read = 0; read < rows; ++read) {
    const std::vector<RibbonGroup*> groups = ordered_groups(read);
    if (groups.empty()) {
      continue;
    }
    if (read != write) {
      for (RibbonGroup* group : groups) {
        row_layouts_[read]->removeWidget(group);
      }
      for (RibbonGroup* group : groups) {
        row_layouts_[write]->insertWidget(row_layouts_[write]->count() - 1, group);
        group->setParent(row_hosts_[write]);
      }
    }
    ++write;
  }
}

// 页高 = 可见排数 × 单排高；多余的空排收起来。
void RibbonPage::apply_rows() {
  compact_rows();  // 先把中间的洞补上，再决定需要几排
  // 拖动时在末尾**多亮一条空排**：那是「新开一排」的落点，也就是"能拖到第三排"的入口。
  const int wanted =
      std::clamp(rows_with_content() + (drop_target_visible_ ? 1 : 0), 1, kMaxRows);
  ensure_rows(wanted);
  for (int row = 0; row < static_cast<int>(row_hosts_.size()); ++row) {
    // 每排等高：落点要按 y 分排，行高就必须是定数，不能由内容撑。
    row_hosts_[row]->setFixedHeight(row_height_);
    row_hosts_[row]->setVisible(row < wanted);
  }
  visible_rows_ = wanted;
  const int height = wanted * row_height_;
  setFixedHeight(height);
  // 拖动期间 set_drop_target_visible(true) 每次 dragMove 都会被叫一遍；
  // 高度没变就别通知外层，不然每动一下鼠标都重排一次主窗口。
  if (height != applied_height_) {
    applied_height_ = height;
    updateGeometry();  // 收缩时也告诉父布局：页高变了（不进这一步，Ribbon 会留一条空白）
    if (QWidget* parent = parentWidget()) {
      parent->updateGeometry();
    }
    emit rows_changed();
  }
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
  // 每一排都摘一遍：removeWidget 不在的话是空操作。别赌"它一定在 row_of 说的那一排"。
  for (auto* layout : row_layouts_) {
    layout->removeWidget(group);
  }
  apply_rows();
  return Slot{row, index};
}

void RibbonPage::insert_group(RibbonGroup* group, Slot slot) {
  if (group == nullptr) {
    return;
  }
  // 记下摘之前它在哪：落点在自己右边时，摘掉自己会让左边的计数少一个（下一步要减回去）。
  int old_row = -1;
  int old_index = -1;
  // 每一排都摘一遍，免得同一个控件在两个布局里各留一条项——留下"幽灵项"的话，
  // 那一排会被算成非空，于是永远收不起来。
  for (int r = 0; r < static_cast<int>(row_layouts_.size()); ++r) {
    if (const int at = row_layouts_[r]->indexOf(group); at >= 0) {
      old_row = r;
      old_index = at;
    }
    row_layouts_[r]->removeWidget(group);
  }
  const int row = std::clamp(slot.row, 0, kMaxRows - 1);
  ensure_rows(row + 1);  // 拖到还没存在的那一排，也得先把它建出来
  int index = slot.index;
  if (old_row == row && old_index >= 0 && old_index < index) {
    --index;  // 自己已经摘掉了，落点左边少一个
  }
  const std::vector<RibbonGroup*> groups = ordered_groups(row);
  const int clamped = std::clamp(index, 0, static_cast<int>(groups.size()));
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
  for (int row = 0; row < static_cast<int>(row_layouts_.size()); ++row) {
    total += static_cast<int>(ordered_groups(row).size());
  }
  return total;
}

std::vector<RibbonPage::Placement> RibbonPage::placements() const {
  std::vector<Placement> out;
  for (int row = 0; row < static_cast<int>(row_layouts_.size()); ++row) {
    const std::vector<RibbonGroup*> groups = ordered_groups(row);
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
      out.push_back(Placement{groups[i], Slot{row, i}});
    }
  }
  return out;
}

std::vector<RibbonGroup*> RibbonPage::all_groups() const {
  // 先按页面上的顺序，再把浮动出去的补在后面——回放时要看到全量，不能漏掉任何一组。
  std::vector<RibbonGroup*> out;
  for (const Placement& placement : placements()) {
    out.push_back(placement.group);
  }
  for (RibbonGroup* group : groups_by_id_) {
    if (group != nullptr && std::find(out.begin(), out.end(), group) == out.end()) {
      out.push_back(group);
    }
  }
  return out;
}

void RibbonPage::detach_all_groups() {
  // 只动**挂在页面上**的那些：浮动出去的分组住在浮窗里，不能碰（碰了就把浮窗掏空）。
  std::vector<RibbonGroup*> docked;
  for (const Placement& placement : placements()) {
    docked.push_back(placement.group);
  }
  // 每一排都摘一遍（removeWidget 不在的话是空操作），最后只 apply_rows 一次：
  // 逐个 detach 会把页高来回算几十遍。
  for (QHBoxLayout* layout : row_layouts_) {
    for (RibbonGroup* group : docked) {
      if (group != nullptr) {
        layout->removeWidget(group);
      }
    }
  }
  // 摘下来的控件还挂在原来的排宿主下面，不隐藏的话会浮在页面上；insert_group 会 show()。
  for (RibbonGroup* group : docked) {
    group->hide();
  }
  apply_rows();
}

void RibbonPage::append_group(RibbonGroup* group) {
  if (group == nullptr) {
    return;
  }
  int row = 0;
  for (int r = 0; r < static_cast<int>(row_layouts_.size()); ++r) {
    if (!ordered_groups(r).empty()) {
      row = r;
    }
  }
  insert_group(group, Slot{row, static_cast<int>(ordered_groups(row).size())});
}

RibbonPage::Slot RibbonPage::drop_slot_at(const QPoint& content_pos) const {
  // 每排等高，所以排号就是 y 除以排高——不去读还没更新完的几何。
  const int rows = (std::max)(1, visible_rows_);
  const int height = (std::max)(1, row_height_);
  const int row = std::clamp(content_pos.y() / height, 0, rows - 1);
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
  for (int row = 0; row < static_cast<int>(row_layouts_.size()); ++row) {
    if (row_layouts_[row]->indexOf(group) >= 0) {
      return row;
    }
  }
  return -1;
}

std::vector<RibbonGroup*> RibbonPage::ordered_groups(int row) const {
  std::vector<RibbonGroup*> groups;
  if (row < 0 || row >= static_cast<int>(row_layouts_.size())) {
    return groups;
  }
  QHBoxLayout* layout = row_layouts_[row];
  for (int i = 0; i < layout->count(); ++i) {
    if (auto* group = qobject_cast<RibbonGroup*>(layout->itemAt(i)->widget())) {
      // 只认真正挂在这一排下面的：布局项可能还留着上一个位置的空壳。
      if (group->parentWidget() == row_hosts_[row]) {
        groups.push_back(group);
      }
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
  const int row = std::clamp(slot.row, 0, static_cast<int>(row_hosts_.size()) - 1);
  if (row < 0) {
    return;
  }
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
  for (int row = 0; row < static_cast<int>(row_layouts_.size()); ++row) {
    const std::vector<RibbonGroup*> groups = ordered_groups(row);
    for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
      groups[i]->set_separator_visible(i + 1 != static_cast<int>(groups.size()));
    }
  }
}

}  // namespace tamias
