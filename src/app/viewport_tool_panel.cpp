#include "viewport_tool_panel.h"

#include "floor_panel.h"
#include "visibility_panel.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace tamias {
namespace {

constexpr int kPad = 6;
constexpr int kButtonSize = 36;
constexpr int kGap = 6;
constexpr int kRailGap = 10;    // 按钮列与功能页之间的空隙（中间画竖分割线）
constexpr int kPageWidth = 272; // 功能页宽度

QIcon tinted_mask_icon(const QString& resource, const QColor& color) {
  const QIcon source(resource);
  QIcon result;
  const int sizes[] = {16, 18, 20, 32};
  for (int size : sizes) {
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

}  // namespace

ViewportToolPanel::ViewportToolPanel(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAutoFillBackground(false);
  theme_ = theme_palette();
  setFixedWidth(preferred_width());

  icon_2d_ = load_icon(QStringLiteral(":/icons/view_2d.svg"));
  icon_3d_ = load_icon(QStringLiteral(":/icons/view_3d.svg"));

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(kPad, kPad, kPad, kPad);
  root->setSpacing(0);

  // 左列：按钮。
  rail_ = new QWidget(this);
  rail_->setFixedWidth(kButtonSize);
  auto* rail_layout = new QVBoxLayout(rail_);
  rail_layout->setContentsMargins(0, 0, 0, 0);
  rail_layout->setSpacing(kGap);

  plan_button_ =
      add_rail_button(rail_layout, icon_3d_, tr("Switch between 2D top view (Y up) and 3D perspective"));
  plan_button_->setCheckable(true);
  connect(plan_button_, &QToolButton::toggled, this, [this](bool plan) {
    plan_button_->setIcon(plan ? icon_2d_ : icon_3d_);
    plan_button_->setToolTip(plan ? tr("2D top view, Y up") : tr("3D perspective"));
    emit plan_view_toggled(plan);
  });

  // 构件显隐：一键开合右功能页，不再走"眼睛 → 类别子菜单 → 勾选"三步。
  visibility_button_ = add_rail_button(rail_layout, load_icon(QStringLiteral(":/icons/visibility.svg")),
                                       tr("Show or hide components by category"));
  visibility_button_->setCheckable(true);
  connect(visibility_button_, &QToolButton::toggled, this, [this](bool open) {
    set_active_page(open ? kVisibilityPage : -1);
  });

  // 按楼层显隐：和构件显隐一样是同一列里的功能页（勾选 = 显示）。
  floor_button_ = add_rail_button(rail_layout, load_icon(QStringLiteral(":/icons/storey.svg")),
                                  tr("Show or hide floors, and set floor heights"));
  floor_button_->setCheckable(true);
  connect(floor_button_, &QToolButton::toggled, this, [this](bool open) {
    set_active_page(open ? kFloorPage : -1);
  });

  auto* sep = new QFrame(rail_);
  sep->setFrameShape(QFrame::HLine);
  sep->setFixedHeight(1);
  sep->setStyleSheet(
      QStringLiteral("background: %1; border: none;").arg(css_color(theme_.divider)));
  rail_layout->addWidget(sep);

  auto* frame_button =
      add_rail_button(rail_layout, load_icon(QStringLiteral(":/icons/frame_all.svg")), tr("Fit All"));
  connect(frame_button, &QToolButton::clicked, this, &ViewportToolPanel::frame_all_clicked);

  rail_layout->addStretch(1);
  root->addWidget(rail_);

  // 右侧：功能页（收起时不占位）。嵌进来的面板跟着整列一起走系统深浅色。
  pages_ = new QStackedWidget(this);
  pages_->setFixedWidth(kPageWidth);
  visibility_page_ = new VisibilityPanel(pages_);
  visibility_page_->set_dark_theme(is_dark_theme());
  pages_->addWidget(visibility_page_);
  floor_page_ = new FloorPanel(pages_);
  floor_page_->set_dark_theme(is_dark_theme());
  pages_->addWidget(floor_page_);
  pages_->setCurrentIndex(kVisibilityPage);
  pages_->hide();
  root->addWidget(pages_);

  setStyleSheet(QStringLiteral(
                    "QToolButton#viewportRailButton {"
                    "  background: transparent; border: none; border-radius: 6px; padding: 0px;"
                    "}"
                    "QToolButton#viewportRailButton:hover { background: %1; }"
                    "QToolButton#viewportRailButton:checked, QToolButton#viewportRailButton:pressed {"
                    "  background: %2;"
                    "}"
                    "QToolButton#viewportRailButton::menu-indicator { image: none; width: 0; }")
                    .arg(css_color(theme_.hover), css_color(theme_.checked)));
}

int ViewportToolPanel::preferred_width() const {
  return kPad * 2 + kButtonSize + (panel_open() ? kRailGap + kPageWidth : 0);
}

void ViewportToolPanel::set_viewport(DocumentViewport* viewport) {
  if (visibility_page_ != nullptr) {
    visibility_page_->set_viewport(viewport);
  }
  if (floor_page_ != nullptr) {
    floor_page_->set_viewport(viewport);
  }
}

void ViewportToolPanel::toggle_visibility_page() {
  set_active_page(panel_open() ? -1 : kVisibilityPage);
}

void ViewportToolPanel::toggle_floor_page() {
  set_active_page(panel_open() ? -1 : kFloorPage);
}

void ViewportToolPanel::set_active_page(int page) {
  if (active_page_ == page) {
    return;
  }
  active_page_ = page;
  pages_->setVisible(page >= 0);
  if (page >= 0) {
    pages_->setCurrentIndex(page);
    if (page == kVisibilityPage) {
      visibility_page_->refresh();
    } else if (page == kFloorPage) {
      floor_page_->refresh();
    }
  }
  {
    // 按钮按下态跟着页面走，但别再把 toggled 弹回来。
    const QSignalBlocker blocker(visibility_button_);
    visibility_button_->setChecked(page == kVisibilityPage);
  }
  {
    const QSignalBlocker blocker(floor_button_);
    floor_button_->setChecked(page == kFloorPage);
  }
  apply_width();
  emit layout_changed();
}

void ViewportToolPanel::apply_width() {
  // 整列宽 = 按钮列（+ 功能页），视口左余下的空间让给三维区域。
  setFixedWidth(preferred_width());
}

void ViewportToolPanel::set_plan_view(bool plan) {
  if (plan_button_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(plan_button_);
  plan_button_->setChecked(plan);
  plan_button_->setIcon(plan ? icon_2d_ : icon_3d_);
  plan_button_->setToolTip(plan ? tr("2D top view, Y up") : tr("3D perspective"));
}

QToolButton* ViewportToolPanel::add_rail_button(QVBoxLayout* layout, const QIcon& icon,
                                                const QString& tip) {
  auto* button = new QToolButton(rail_);
  button->setObjectName(QStringLiteral("viewportRailButton"));
  button->setAttribute(Qt::WA_NativeWindow);
  button->setIcon(icon);
  button->setIconSize(QSize(18, 18));
  button->setFixedSize(kButtonSize, kButtonSize);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::PointingHandCursor);
  button->setToolTip(tip);
  layout->addWidget(button, 0, Qt::AlignHCenter);
  return button;
}

QIcon ViewportToolPanel::load_icon(const QString& resource) const {
  return tinted_mask_icon(resource, theme_.icon);
}

void ViewportToolPanel::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), theme_.surface);
  // 和三维区域之间一条分隔线；整列贴边，所以不做圆角。
  painter.setPen(QPen(theme_.border, 1.0));
  painter.drawLine(QPointF(0.5, 0.0), QPointF(0.5, static_cast<qreal>(height())));
  if (panel_open()) {
    const qreal x = kPad + kButtonSize + kRailGap / 2.0;
    painter.setPen(QPen(theme_.divider, 1.0));
    painter.drawLine(QPointF(x, kPad + 2.0), QPointF(x, height() - kPad - 2.0));
  }
}

}  // namespace tamias
