#include "box_select_overlay.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QShowEvent>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <QBitmap>
#endif

#include <algorithm>

namespace tamias {

BoxSelectOverlay::BoxSelectOverlay(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  // Native HWND so this overlay stacks above the Vulkan/OpenGL surface child.
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAutoFillBackground(false);
  hide();
}

void BoxSelectOverlay::set_box(const QRect& rect, bool) {
  const QRect box = rect.normalized();
  if (box.width() < 1 && box.height() < 1) {
    hide();
    return;
  }
  setGeometry(box);
  show();
  raise();
  apply_wire_mask();
  update();
}

void BoxSelectOverlay::hide_box() { hide(); }

void BoxSelectOverlay::paintEvent(QPaintEvent*) {
  // Window region is already a 1px hollow frame; fill it solid white so
  // unpainted pixels do not show through as black/blue.
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.fillRect(rect(), Qt::white);
}

void BoxSelectOverlay::resizeEvent(QResizeEvent*) { apply_wire_mask(); }

void BoxSelectOverlay::showEvent(QShowEvent*) { apply_wire_mask(); }

void BoxSelectOverlay::apply_wire_mask() {
  const int lw = width();
  const int lh = height();
  if (lw <= 0 || lh <= 0) {
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
  if (pw <= 0 || ph <= 0) {
    return;
  }
  const int t = (std::max)(1, qRound(devicePixelRatioF()));
  HRGN outer = CreateRectRgn(0, 0, pw, ph);
  if (pw > t * 2 && ph > t * 2) {
    HRGN inner = CreateRectRgn(t, t, pw - t, ph - t);
    CombineRgn(outer, outer, inner, RGN_DIFF);
    DeleteObject(inner);
  }
  SetWindowRgn(hwnd, outer, TRUE);
#else
  const qreal dpr = devicePixelRatioF();
  QBitmap bitmap((size() * dpr).toSize());
  bitmap.setDevicePixelRatio(dpr);
  bitmap.fill(Qt::color0);
  QPainter mp(&bitmap);
  mp.setBrush(Qt::NoBrush);
  QPen pen(Qt::color1, 1);
  mp.setPen(pen);
  mp.drawRect(QRectF(rect()).adjusted(0, 0, -1, -1));
  setMask(bitmap);
#endif
}

}  // namespace tamias
