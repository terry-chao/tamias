#include "app/shell/ribbon_group.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace tamias {
namespace {

// 图标画小一点：贴着视口的时候，工具带上的图标太大会显得头重脚轻（图标本身是
// 彩色轴测插画，缩到 24 还看得清细节）。
constexpr int kIconSize = 24;
// 抓手挪到分组左侧：一条竖着的窄把手，高度跟着分组走。
constexpr int kGripWidth = 8;
constexpr int kTextButtonMinWidth = 52;
// 只有图标的按钮：左右各 4px 内边距（见 RibbonBar 的两份 QSS），最小宽度得把
// 「图标 + 这 8px」都留出来。只写 24 的话，一排挤不下时布局会先压它（带字的按钮
// 最小 52，压不动），压到 24 再扣掉内边距，内容区只剩 16px——比图标还窄，Qt 就把
// 图标缩到 16px 画。结果是一排大按钮里只有「新建 / 打开」两个图标明显小一圈。
constexpr int kIconButtonSidePadding = 4;
constexpr int kIconButtonMinWidth = kIconSize + 2 * kIconButtonSidePadding;
constexpr int kIconButtonMaxWidth = 38;

}  // namespace

RibbonGrip::RibbonGrip(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("ribbonGrip"));
  setFixedWidth(kGripWidth);
  setCursor(Qt::OpenHandCursor);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void RibbonGrip::paintEvent(QPaintEvent*) {
  QColor color = palette().color(QPalette::WindowText);
  color.setAlpha(110);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  const int cx = width() / 2;
  const int cy = height() / 2;
  // 竖把手画成「⋮」：三个点上下排，一眼看出是拖它的地方。
  for (int i = -1; i <= 1; ++i) {
    painter.drawEllipse(QPoint(cx, cy + i * 7), 1, 1);
  }
}

RibbonGroup::RibbonGroup(const QString& title, QWidget* parent)
    : QWidget(parent), title_text_(title) {
  setObjectName(QStringLiteral("ribbonGroup"));
  setAttribute(Qt::WA_StyledBackground, true);
  setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(4, 4, 0, 2);
  root->setSpacing(4);

  // 抓手条：这一组的「拖动把手」，在**左边**——被拖出去就是浮动小工具栏。
  grip_ = new RibbonGrip(this);
  grip_->setToolTip(tr("Drag this group out of the ribbon to float it; drop it back to dock"));
  root->addWidget(grip_);

  auto* body = new QWidget(this);
  auto* body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(2);

  buttons_host_ = new QWidget(body);
  buttons_layout_ = new QHBoxLayout(buttons_host_);
  buttons_layout_->setContentsMargins(0, 0, 0, 0);
  buttons_layout_->setSpacing(2);
  buttons_layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  body_layout->addWidget(buttons_host_, 1);

  title_ = new QLabel(title, body);
  title_->setObjectName(QStringLiteral("ribbonGroupTitle"));
  title_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
  body_layout->addWidget(title_);

  separator_ = new QFrame(this);
  separator_->setObjectName(QStringLiteral("ribbonGroupSep"));
  separator_->setFrameShape(QFrame::VLine);
  separator_->setFixedWidth(1);

  root->addWidget(body);
  root->addWidget(separator_);
}

QToolButton* RibbonGroup::add_action(QAction* action) {
  auto* button = new QToolButton(this);
  button->setObjectName(QStringLiteral("ribbonButton"));
  button->setDefaultAction(action);
  button->setIconSize(QSize(kIconSize, kIconSize));
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::PointingHandCursor);
  button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
  buttons_layout_->addWidget(button, 0, Qt::AlignTop);
  apply_style_to(button);
  return button;
}

// 一个按钮的图标 / 文字形态：整条带子的大形态（mode_），或者**这一个**按钮自己的
// 例外——action 上挂 ribbonIconOnly 的只画图标（图标本身已经说清楚它是什么，
// 名字留在悬浮提示与菜单里；见 MainWindow 里那几个 action）。
void RibbonGroup::apply_style_to(QToolButton* button) const {
  const QAction* action = button->defaultAction();
  const bool per_button = action != nullptr && action->property("ribbonIconOnly").toBool();
  const bool icon_only = mode_ == RibbonDisplayMode::IconOnly || per_button;
  // 按钮外框（内边距）也得跟着形态走：只有图标的按钮留一圈小边，不然图标缩了、
  // 外框还是原来那么大，一排看过去宽度没变，图标反而显得空荡荡的。
  // 样式表里按这个属性选更紧的内边距，见 RibbonBar 的两份 QSS。
  // 「不写字、旁边却有带字按钮」是另一种情形（比如「设置」那颗挨着一排带字的）。
  // 这时候外框要撑到和带字按钮一样高，矮一截会显得这个图标异常小；图标仍和旁边
  // 那排图标齐平，只是下面空着标签的位置（QSS 里那条 ribbonLabelLess）。
  // 整条带子都是图标时才用紧凑的小外框。
  const bool label_less_with_neighbours = per_button && mode_ == RibbonDisplayMode::IconWithText;
  if (button->property("ribbonIconOnly").toBool() != icon_only ||
      button->property("ribbonLabelLess").toBool() != label_less_with_neighbours) {
    button->setProperty("ribbonIconOnly", icon_only);
    button->setProperty("ribbonLabelLess", label_less_with_neighbours);
    button->style()->unpolish(button);
    button->style()->polish(button);
  }
  button->setToolButtonStyle(icon_only ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
  button->setIconSize(QSize(kIconSize, kIconSize));
  button->setMinimumWidth(icon_only ? kIconButtonMinWidth : kTextButtonMinWidth);
  button->setMaximumWidth(icon_only ? kIconButtonMaxWidth : QWIDGETSIZE_MAX);
}

void RibbonGroup::reorder_buttons(const std::vector<QToolButton*>& ordered) {
  if (ordered.empty()) {
    return;
  }
  int insert_at = buttons_layout_->count();
  for (QToolButton* button : ordered) {
    if (button == nullptr) {
      continue;
    }
    const int index = buttons_layout_->indexOf(button);
    if (index >= 0) {
      insert_at = (std::min)(insert_at, index);
    }
  }
  if (insert_at >= buttons_layout_->count()) {
    return;
  }
  for (QToolButton* button : ordered) {
    if (button != nullptr) {
      buttons_layout_->removeWidget(button);
    }
  }
  for (QToolButton* button : ordered) {
    if (button != nullptr) {
      buttons_layout_->insertWidget(insert_at++, button, 0, Qt::AlignTop);
    }
  }
}

void RibbonGroup::set_separator_visible(bool visible) {
  separator_->setVisible(visible);
}

void RibbonGroup::set_display_mode(RibbonDisplayMode mode) {
  mode_ = mode;
  apply_display_mode();
}

void RibbonGroup::set_identity(const QString& page_id, const QString& group_id) {
  page_id_ = page_id;
  group_id_ = group_id;
}

void RibbonGroup::set_floating(bool floating) {
  floating_ = floating;
  grip_->setVisible(!floating_);
  if (floating_) {
    // 浮窗里不需要分组之间那条竖线；回页时 RibbonPage 会重新算。
    separator_->setVisible(false);
  }
  apply_display_mode();
}

void RibbonGroup::apply_display_mode() {
  const bool icon_only = mode_ == RibbonDisplayMode::IconOnly;
  // 浮起来以后标题挂在浮窗的标题栏上，分组自己就不再重复一个字。
  title_->setVisible(!floating_ && !icon_only);
  for (QToolButton* button : buttons()) {
    apply_style_to(button);
  }
}

std::vector<QToolButton*> RibbonGroup::buttons() const {
  std::vector<QToolButton*> result;
  for (int i = 0; i < buttons_layout_->count(); ++i) {
    if (auto* button = qobject_cast<QToolButton*>(buttons_layout_->itemAt(i)->widget())) {
      result.push_back(button);
    }
  }
  return result;
}

void RibbonGroup::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  // 按在分组自己身上（抓手、标题、按钮之间的空隙）就是要拖它；按在按钮上按钮会自己吃掉事件。
  armed_ = true;
  press_pos_in_group_ = event->position().toPoint();
  event->accept();
}

void RibbonGroup::mouseMoveEvent(QMouseEvent* event) {
  if (!armed_) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  if (!(event->buttons() & Qt::LeftButton)) {
    armed_ = false;
    return;
  }
  if ((event->position().toPoint() - press_pos_in_group_).manhattanLength() <
      QApplication::startDragDistance()) {
    event->accept();
    return;
  }
  armed_ = false;
  event->accept();
  begin_drag(this, press_pos_in_group_);
}

void RibbonGroup::mouseReleaseEvent(QMouseEvent* event) {
  armed_ = false;
  QWidget::mouseReleaseEvent(event);
}

void RibbonGroup::begin_drag(QWidget* source, const QPoint& press_pos_in_source) {
  if (source == nullptr || page_id_.isEmpty() || group_id_.isEmpty()) {
    return;
  }
  const QPoint hotspot = mapFrom(source, press_pos_in_source);
  auto* mime = new QMimeData;
  mime->setData(QString::fromLatin1(kRibbonGroupMimeType),
                (page_id_ + QLatin1Char('|') + group_id_).toUtf8());
  auto* drag = new QDrag(source);
  drag->setMimeData(mime);
  drag->setPixmap(grab());
  drag->setHotSpot(hotspot);
  // 落在 Ribbon 上：RibbonBar 的 dropEvent 已经把它放好了；没落上就交给窗口决定去留。
  const Qt::DropAction action = drag->exec(Qt::MoveAction);
  if (action == Qt::MoveAction) {
    return;
  }
  emit drag_dropped_outside(this, QCursor::pos() - hotspot);
}

}  // namespace tamias
