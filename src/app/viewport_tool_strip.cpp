#include "viewport_tool_strip.h"

#include <QBitmap>
#include <QFrame>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QShowEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tamias {
namespace {

constexpr int kPlateRadius = 12;
constexpr int kPlateWidth = 96;  // match ViewCube
constexpr int kPad = 10;
constexpr int kButtonSize = 36;
constexpr int kGap = 6;
const QColor kPlateBg(42, 45, 52);

QIcon tinted_mask_icon(const QString& resource, const QColor& color) {
  const QIcon source(resource);
  QIcon result;
  const int sizes[] = {16, 20, 32};
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

ViewportToolStrip::ViewportToolStrip(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAutoFillBackground(false);
  setFixedWidth(kPlateWidth);

  icon_2d_ = load_icon(QStringLiteral(":/icons/view_2d.svg"));
  icon_3d_ = load_icon(QStringLiteral(":/icons/view_3d.svg"));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kPad, kPad, kPad, kPad);
  layout->setSpacing(kGap);

  plan_button_ = add_button(layout, icon_3d_,
                            tr("Switch between 2D top view (Y up) and 3D perspective"));
  plan_button_->setCheckable(true);
  connect(plan_button_, &QToolButton::toggled, this, [this](bool plan) {
    plan_button_->setIcon(plan ? icon_2d_ : icon_3d_);
    plan_button_->setToolTip(plan ? tr("2D top view, Y up") : tr("3D perspective"));
    emit plan_view_toggled(plan);
  });

  auto* visibility_button =
      add_button(layout, load_icon(QStringLiteral(":/icons/visibility.svg")),
                 tr("Hide or isolate components"));
  visibility_menu_ = new QMenu(this);
  visibility_button->setMenu(visibility_menu_);
  visibility_button->setPopupMode(QToolButton::InstantPopup);
  connect(visibility_menu_, &QMenu::aboutToShow, this, &ViewportToolStrip::visibility_menu_about_to_show);

  auto* floor_button =
      add_button(layout, load_icon(QStringLiteral(":/icons/storey.svg")), tr("Filter by floor"));
  floor_menu_ = new QMenu(this);
  floor_button->setMenu(floor_menu_);
  floor_button->setPopupMode(QToolButton::InstantPopup);
  connect(floor_menu_, &QMenu::aboutToShow, this, &ViewportToolStrip::floor_menu_about_to_show);

  auto* sep = new QFrame(this);
  sep->setFrameShape(QFrame::HLine);
  sep->setFixedHeight(1);
  sep->setStyleSheet(QStringLiteral("background: #3a3c42; border: none;"));
  layout->addWidget(sep);

  auto* frame_button =
      add_button(layout, load_icon(QStringLiteral(":/icons/frame_all.svg")), tr("Fit All"));
  connect(frame_button, &QToolButton::clicked, this, &ViewportToolStrip::frame_all_clicked);

  setFixedSize(kPlateWidth, sizeHint().height());

  setStyleSheet(QStringLiteral(
      "QToolButton#viewportStripButton {"
      "  background: transparent; border: none; border-radius: 8px; padding: 2px;"
      "}"
      "QToolButton#viewportStripButton:hover { background: rgba(255, 255, 255, 28); }"
      "QToolButton#viewportStripButton:checked, QToolButton#viewportStripButton:pressed {"
      "  background: rgba(47, 125, 222, 90);"
      "}"
      "QToolButton#viewportStripButton::menu-indicator { image: none; width: 0; }"));
}

void ViewportToolStrip::set_plan_view(bool plan) {
  if (plan_button_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(plan_button_);
  plan_button_->setChecked(plan);
  plan_button_->setIcon(plan ? icon_2d_ : icon_3d_);
  plan_button_->setToolTip(plan ? tr("2D top view, Y up") : tr("3D perspective"));
}

QToolButton* ViewportToolStrip::add_button(QVBoxLayout* layout, const QIcon& icon,
                                           const QString& tip) {
  auto* button = new QToolButton(this);
  button->setObjectName(QStringLiteral("viewportStripButton"));
  button->setAttribute(Qt::WA_NativeWindow);
  button->setIcon(icon);
  button->setIconSize(QSize(20, 20));
  button->setFixedSize(kButtonSize, kButtonSize);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setAutoRaise(true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::PointingHandCursor);
  button->setToolTip(tip);
  layout->addWidget(button, 0, Qt::AlignHCenter);
  return button;
}

QIcon ViewportToolStrip::load_icon(const QString& resource) const {
  return tinted_mask_icon(resource, QColor(216, 212, 206));
}

void ViewportToolStrip::apply_plate_mask() {
  // Don't stamp a region from a stub HWND — that clipped the strip to a tiny cap.
  if (width() < 8 || height() < 8) {
    return;
  }
#if defined(_WIN32)
  const WId wid = winId();
  if (!wid) {
    return;
  }
  auto* hwnd = reinterpret_cast<HWND>(wid);
  RECT rc{};
  if (!GetClientRect(hwnd, &rc)) {
    return;
  }
  const int pw = rc.right - rc.left;
  const int ph = rc.bottom - rc.top;
  if (pw < 8 || ph < 8) {
    return;
  }
  const int dia = (std::max)(2, qRound(kPlateRadius * 2.0 * devicePixelRatioF()));
  HRGN hrgn = CreateRoundRectRgn(0, 0, pw + 1, ph + 1, dia, dia);
  SetWindowRgn(hwnd, hrgn, TRUE);
#else
  const qreal dpr = devicePixelRatioF();
  QBitmap bitmap((size() * dpr).toSize());
  bitmap.setDevicePixelRatio(dpr);
  bitmap.fill(Qt::color0);
  QPainter mp(&bitmap);
  mp.setBrush(Qt::color1);
  mp.setPen(Qt::NoPen);
  mp.drawRoundedRect(QRectF(rect()), kPlateRadius, kPlateRadius);
  setMask(bitmap);
#endif
}

void ViewportToolStrip::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  apply_plate_mask();
}

void ViewportToolStrip::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  apply_plate_mask();
}

void ViewportToolStrip::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), kPlateBg);
  QPainterPath rim;
  rim.addRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), kPlateRadius - 1.0,
                     kPlateRadius - 1.0);
  painter.setPen(QPen(QColor(255, 255, 255, 28), 1.0));
  painter.drawPath(rim);
}

}  // namespace tamias
