#include "app/shell/ribbon_page.h"

#include "app/shell/ribbon_group.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <functional>

namespace tamias {
namespace {

// 行高按「最高的一颗按钮 + 分组标题」来给，不留富余：按钮 24px 图标 + 一行字
// 大约 48px，加分组标题 15px、上下边距 6px，74 刚好贴住。以前按图标 28px、两行
// 文字留到 92，图标缩下来以后底下就空出一大块，看着像还留着文字的位置。
constexpr int kRowHeightText = 74;
constexpr int kRowHeightIconOnly = 40;
constexpr int kEdgeMargin = 4;
// 分区标记的宽度：3px 主色竖线 + 7px 淡淡的同色底。名字不画在带上（太占地方，
// 也开始 / 视图 这种名字看菜单就知道），改成悬浮提示。
constexpr int kRailWidth = 10;

}  // namespace

// 分区标记：左边缘一条主色竖线（贯穿这一段的全部排）+ 一点点同色底。
// 一条带子上并排着几段（开始 / 视图 / 插件页…），靠这一条线区分；
// 段名不画出来，鼠标停在线上的提示里给（见 RibbonPage::update_section_tooltip）。
class RibbonSectionRail final : public QWidget {
 public:
  explicit RibbonSectionRail(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("ribbonSectionRail"));
    setFixedWidth(kRailWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
  }

  void set_accent(const QColor& accent) {
    if (!accent.isValid()) {
      return;
    }
    accent_ = accent;
    update();
  }

  void set_double_click_handler(std::function<void()> handler) {
    on_double_click_ = std::move(handler);
  }

 protected:
  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (on_double_click_) {
      on_double_click_();
    }
    event->accept();
  }

  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    QColor tint = accent_;
    tint.setAlpha(26);
    painter.fillRect(rect(), tint);
    QColor edge = accent_;
    edge.setAlpha(240);
    painter.fillRect(QRect(0, 0, 3, height()), edge);
  }

 private:
  QColor accent_ = QColor(0x1a, 0x73, 0xe8);
  std::function<void()> on_double_click_;
};

// 工具带自己的滚动区：一排工具放不下时**用滚轮横着滚**。QScrollArea 默认把滚轮
// 当纵向滚动，而这里的纵向滚动条是关着的——不接这一下，鼠标在带上滚就是没反应，
// 右边被挡住的组（设置 / 插件 / 帮助…）就不好够。
class RibbonBandScroll final : public QScrollArea {
 public:
  using QScrollArea::QScrollArea;

 protected:
  void wheelEvent(QWheelEvent* event) override {
    QScrollBar* bar = horizontalScrollBar();
    if (bar == nullptr || bar->maximum() <= 0) {
      QScrollArea::wheelEvent(event);
      return;
    }
    const int steps = event->angleDelta().y() != 0 ? event->angleDelta().y()
                                                   : event->angleDelta().x();
    bar->setValue(bar->value() - steps / 3);
    event->accept();
  }
};

RibbonPage::RibbonPage(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("ribbonPage"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // 分区标题栏：只有本页是工具带上的一段时才露面（见 set_section_chrome_visible）。
  section_rail_ = new RibbonSectionRail(this);
  section_rail_->hide();
  section_rail_->set_double_click_handler([this] { emit section_header_double_clicked(); });
  root->addWidget(section_rail_);

  auto* scroll = new RibbonBandScroll(this);
  scroll->setObjectName(QStringLiteral("ribbonPageScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFocusPolicy(Qt::NoFocus);
  scroll_ = scroll;

  content_ = new QWidget(scroll);
  content_->setObjectName(QStringLiteral("ribbonPageContent"));
  rows_layout_ = new QVBoxLayout(content_);
  rows_layout_->setContentsMargins(0, 0, 0, 0);
  rows_layout_->setSpacing(0);
  ensure_rows(1);
  scroll->setWidget(content_);

  root->addWidget(scroll, 1);

  // 横向滚动条一出现 / 一消失，页高要跟着变（见 horizontal_bar_height）。
  if (QScrollBar* bar = scroll->horizontalScrollBar()) {
    connect(bar, &QScrollBar::rangeChanged, this, [this](int, int) { apply_rows(); });
  }
  apply_rows();
}

void RibbonPage::set_section_title(const QString& title) {
  section_title_ = title;
  update_section_tooltip();
}

void RibbonPage::set_section_tooltip(const QString& text) {
  section_tooltip_ = text;
  update_section_tooltip();
}

// 段名不画在带上（太占地方），挂在竖线的悬浮提示里：第一行是段名，第二行是
// 「双击卷起」那句（RibbonBar 给的现成文案）。
void RibbonPage::update_section_tooltip() {
  if (section_rail_ == nullptr) {
    return;
  }
  if (section_title_.isEmpty()) {
    section_rail_->setToolTip(section_tooltip_);
    return;
  }
  if (section_tooltip_.isEmpty()) {
    section_rail_->setToolTip(section_title_);
    return;
  }
  section_rail_->setToolTip(QStringLiteral("%1\n%2").arg(section_title_, section_tooltip_));
}

void RibbonPage::set_section_accent(const QColor& accent) {
  if (!accent.isValid()) {
    return;
  }
  section_accent_ = accent;
  if (section_rail_ != nullptr) {
    section_rail_->set_accent(accent);
  }
}

void RibbonPage::set_section_chrome_visible(bool visible) {
  section_chrome_visible_ = visible;
  if (section_rail_ != nullptr) {
    section_rail_->setVisible(visible);
  }
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
  // 行高只是下限：贴住「24px 图标 + 一行字 + 分组标题」那点内容。系统字体放大时
  // 按钮会比这个常数更高，按内容的自然高度兜底，别把按钮压扁裁掉。
  hint.setHeight((std::max)(visible_rows_ * row_height_, hint.height()));
  return hint;
}

QSize RibbonPage::minimumSizeHint() const {
  QSize hint = QWidget::minimumSizeHint();
  hint.setHeight((std::max)(visible_rows_ * row_height_, hint.height()));
  return hint;
}

int RibbonPage::horizontal_bar_height() const {
  if (scroll_ == nullptr) {
    return 0;
  }
  const QScrollBar* bar = scroll_->horizontalScrollBar();
  if (bar == nullptr || bar->maximum() <= bar->minimum()) {
    return 0;
  }
  // 用 sizeHint 而不是 height()：滚动条刚露头那一下 height() 还是 0。
  return bar->sizeHint().height();
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
  // 页高要**连横向滚动条一起算**：滚动条是从页里切走一条空间画的，只按「排数 × 排高」
  // 给高度的话，一旦出滚动条，排里最下面那条组名就被压在滚动条底下（92 的排高只剩 80 能看）。
  const int bar_height = horizontal_bar_height();
  const int height = wanted * row_height_ + bar_height;
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
