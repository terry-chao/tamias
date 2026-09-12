#include "drawing_view.h"

#include "drawing_document.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace tamias {
namespace {

constexpr double kMinZoom = 1e-4;
constexpr double kMaxZoom = 1e6;
const QColor kCanvasDark(30, 33, 39);
const QColor kCanvasLight(246, 246, 248);
const QColor kHudText(150, 156, 168);

}  // namespace

DrawingView::DrawingView(std::unique_ptr<DrawingDocument> document, QWidget* parent)
    : QWidget(parent), document_(std::move(document)) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAutoFillBackground(false);
  center_page();
}

DrawingView::~DrawingView() = default;

QString DrawingView::title() const {
  return document_ ? document_->title() : QString();
}

QTransform DrawingView::world_to_device() const {
  QTransform transform;
  transform.translate(width() * 0.5, height() * 0.5);
  transform.scale(zoom_, -zoom_);  // 世界 Y 向上 → 屏幕 Y 向下
  transform.translate(-center_.x(), -center_.y());
  return transform;
}

QPointF DrawingView::to_world(const QPointF& device) const {
  bool ok = false;
  const QTransform inverse = world_to_device().inverted(&ok);
  return ok ? inverse.map(device) : QPointF();
}

void DrawingView::center_page() {
  if (!document_) {
    return;
  }
  const QRectF page = document_->page_rect(page_);
  center_ = page.center();
}

void DrawingView::fit_to_window() {
  if (!document_ || width() <= 2 || height() <= 2) {
    return;
  }
  const QRectF page = document_->page_rect(page_);
  if (page.width() <= 0.0 || page.height() <= 0.0) {
    return;
  }
  const double margin = 24.0;
  const double scale_x = std::max(1.0, width() - margin) / page.width();
  const double scale_y = std::max(1.0, height() - margin) / page.height();
  zoom_ = std::clamp(std::min(scale_x, scale_y), kMinZoom, kMaxZoom);
  center_ = page.center();
  update_status();
  update();
}

void DrawingView::zoom_in() { zoom_by(1.25, QPointF(width() * 0.5, height() * 0.5)); }

void DrawingView::zoom_out() { zoom_by(0.8, QPointF(width() * 0.5, height() * 0.5)); }

void DrawingView::reset_zoom() {
  zoom_by(1.0 / std::max(zoom_, kMinZoom), QPointF(width() * 0.5, height() * 0.5));
}

void DrawingView::zoom_by(double factor, const QPointF& anchor) {
  user_adjusted_ = true;
  const QPointF before = to_world(anchor);
  zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
  const QPointF after = to_world(anchor);
  center_ += before - after;
  update_status();
  update();
}

void DrawingView::set_page(int page) {
  if (!document_) {
    return;
  }
  const int clamped = std::clamp(page, 0, std::max(0, document_->page_count() - 1));
  if (clamped == page_) {
    return;
  }
  page_ = clamped;
  center_page();
  if (zoom_ > 0.0) {
    fit_to_window();
  }
  update();
}

bool DrawingView::dark_background() const {
  return document_ && document_->dark_background();
}

void DrawingView::toggle_background() {
  if (!document_) {
    return;
  }
  document_->set_dark_background(!document_->dark_background());
  update();
}

void DrawingView::set_layer_visible(int index, bool visible) {
  if (!document_) {
    return;
  }
  document_->set_layer_visible(index, visible);
  update();
}

bool DrawingView::layer_visible(int index) const {
  return document_ && document_->layer_visible(index);
}

void DrawingView::update_status() {
  if (!document_) {
    return;
  }
  QString text = tr("%1%").arg(zoom_ * 100.0, 0, 'f', zoom_ < 1.0 ? 1 : 0);
  if (document_->page_count() > 1) {
    text += tr(" · page %1/%2").arg(page_ + 1).arg(document_->page_count());
  }
  emit status_message(text);
}

void DrawingView::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  // 首次布局前 width()/height() 还不可靠，fit 会算错；所以每次尺寸变化都重算，
  // 直到用户自己缩放过。
  if (!user_adjusted_) {
    fit_to_window();
  }
}

void DrawingView::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.fillRect(rect(),
                   document_ && document_->dark_background() ? kCanvasDark : kCanvasLight);
  if (!document_) {
    return;
  }

  painter.save();
  painter.setTransform(world_to_device());
  document_->paint_page(painter, page_, zoom_);
  painter.restore();

  // 纸面边界
  painter.save();
  painter.setPen(QPen(document_->dark_background() ? QColor(90, 96, 110) : QColor(190, 192, 200),
                      1.0, Qt::DashLine));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(world_to_device().mapRect(document_->page_rect(page_)));
  painter.restore();

  // HUD：光标坐标（图纸绝对坐标）+ 缩放/页码
  const QFontMetrics metrics(font());
  painter.setPen(kHudText);
  if (has_cursor_) {
    const Vec2 origin = document_->world_origin();
    const QString coords = tr("X %1   Y %2")
                               .arg(cursor_world_.x() + origin.x, 0, 'f', 2)
                               .arg(cursor_world_.y() + origin.y, 0, 'f', 2);
    painter.drawText(12, height() - 12, coords);
  }
  QString right = tr("Zoom %1%").arg(zoom_ * 100.0, 0, 'f', zoom_ < 1.0 ? 1 : 0);
  if (document_->page_count() > 1) {
    right += tr("   Page %1/%2").arg(page_ + 1).arg(document_->page_count());
  }
  const int width_right = metrics.horizontalAdvance(right);
  painter.drawText(width() - width_right - 12, height() - 12, right);
}

void DrawingView::wheelEvent(QWheelEvent* event) {
  const int delta = event->angleDelta().y();
  if (delta == 0) {
    QWidget::wheelEvent(event);
    return;
  }
  const double steps = static_cast<double>(delta) / 120.0;
  zoom_by(std::pow(1.2, steps), event->position());
  event->accept();
}

void DrawingView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
    panning_ = true;
    last_drag_ = event->position();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void DrawingView::mouseMoveEvent(QMouseEvent* event) {
  cursor_world_ = to_world(event->position());
  has_cursor_ = true;
  if (panning_) {
    user_adjusted_ = true;
    const QPointF delta = event->position() - last_drag_;
    last_drag_ = event->position();
    const double scale = zoom_ > 0.0 ? zoom_ : 1.0;
    center_ -= QPointF(delta.x() / scale, -delta.y() / scale);
  }
  update();
  QWidget::mouseMoveEvent(event);
}

void DrawingView::mouseReleaseEvent(QMouseEvent* event) {
  if (panning_ && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
    panning_ = false;
    unsetCursor();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void DrawingView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    fit_to_window();
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void DrawingView::leaveEvent(QEvent* event) {
  has_cursor_ = false;
  update();
  QWidget::leaveEvent(event);
}

void DrawingView::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_F:
      fit_to_window();
      return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      zoom_in();
      return;
    case Qt::Key_Minus:
      zoom_out();
      return;
    case Qt::Key_0:
      if (event->modifiers().testFlag(Qt::ControlModifier)) {
        reset_zoom();
        return;
      }
      break;
    case Qt::Key_PageDown:
      set_page(page_ + 1);
      return;
    case Qt::Key_PageUp:
      set_page(page_ - 1);
      return;
    default:
      break;
  }
  QWidget::keyPressEvent(event);
}

void DrawingView::contextMenuEvent(QContextMenuEvent* event) {
  if (!document_) {
    return;
  }
  QMenu menu(this);
  QAction* fit = menu.addAction(tr("Fit to window"));
  connect(fit, &QAction::triggered, this, &DrawingView::fit_to_window);
  QAction* hundred = menu.addAction(tr("Zoom 100%"));
  connect(hundred, &QAction::triggered, this, &DrawingView::reset_zoom);
  QAction* zoom_in_action = menu.addAction(tr("Zoom in"));
  connect(zoom_in_action, &QAction::triggered, this, &DrawingView::zoom_in);
  QAction* zoom_out_action = menu.addAction(tr("Zoom out"));
  connect(zoom_out_action, &QAction::triggered, this, &DrawingView::zoom_out);

  if (document_->page_count() > 1) {
    menu.addSeparator();
    QAction* previous = menu.addAction(tr("Previous page"));
    previous->setEnabled(page_ > 0);
    connect(previous, &QAction::triggered, this, [this] { set_page(page_ - 1); });
    QAction* next = menu.addAction(tr("Next page"));
    next->setEnabled(page_ + 1 < document_->page_count());
    connect(next, &QAction::triggered, this, [this] { set_page(page_ + 1); });
  }

  menu.addSeparator();
  QAction* background = menu.addAction(tr("Light background"));
  background->setCheckable(true);
  background->setChecked(!document_->dark_background());
  connect(background, &QAction::triggered, this, &DrawingView::toggle_background);

  if (document_->layer_count() > 0) {
    menu.addSeparator();
    QMenu* layers = menu.addMenu(tr("Layers"));
    for (int i = 0; i < document_->layer_count(); ++i) {
      const QString name = document_->layer_name(i);
      QAction* action = layers->addAction(name.isEmpty() ? tr("(unnamed)") : name);
      action->setCheckable(true);
      action->setChecked(document_->layer_visible(i));
      connect(action, &QAction::triggered, this, [this, i](bool checked) {
        set_layer_visible(i, checked);
      });
    }
  }
  menu.exec(event->globalPos());
}

}  // namespace tamias
