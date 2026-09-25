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
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace tamias {
namespace {

constexpr int kIconSize = 28;
// 抓手挪到分组左侧：一条竖着的窄把手，高度跟着分组走。
constexpr int kGripWidth = 8;
constexpr int kTextButtonMinWidth = 52;
constexpr int kIconButtonMinWidth = 28;
constexpr int kIconButtonMaxWidth = 44;

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
  button->setToolButtonStyle(mode_ == RibbonDisplayMode::IconOnly ? Qt::ToolButtonIconOnly
                                                                  : Qt::ToolButtonTextUnderIcon);
  button->setIconSize(QSize(kIconSize, kIconSize));
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::PointingHandCursor);
  button->setMinimumWidth(mode_ == RibbonDisplayMode::IconOnly ? kIconButtonMinWidth
                                                              : kTextButtonMinWidth);
  if (mode_ == RibbonDisplayMode::IconOnly) {
    button->setMaximumWidth(kIconButtonMaxWidth);
  }
  button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
  buttons_layout_->addWidget(button, 0, Qt::AlignTop);
  return button;
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
    button->setToolButtonStyle(icon_only ? Qt::ToolButtonIconOnly
                                         : Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setMinimumWidth(icon_only ? kIconButtonMinWidth : kTextButtonMinWidth);
    button->setMaximumWidth(icon_only ? kIconButtonMaxWidth : QWIDGETSIZE_MAX);
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
