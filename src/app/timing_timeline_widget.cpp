#include "timing_timeline_widget.h"

#include "engine/profile/timing_category.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace tamias {
namespace {

constexpr int kPad = 8;
constexpr int kRulerH = 22;
constexpr int kLegendH = 20;
constexpr int kRowH = 26;
constexpr int kBarH = 22;
constexpr int kPanSlop = 4;

int event_depth(const std::vector<TimingEvent>& events, int index) {
  int depth = 0;
  int parent = events[static_cast<std::size_t>(index)].parent;
  while (parent >= 0 && parent < static_cast<int>(events.size()) && depth < 24) {
    ++depth;
    parent = events[static_cast<std::size_t>(parent)].parent;
  }
  return depth;
}

QColor category_color(TimingCategory category, bool selected) {
  QColor color;
  switch (category) {
    case TimingCategory::Command:
      color = QColor(47, 125, 222);
      break;
    case TimingCategory::Modeling:
      color = QColor(214, 126, 44);
      break;
    case TimingCategory::Render:
      color = QColor(46, 160, 110);
      break;
    case TimingCategory::Ui:
      color = QColor(142, 92, 196);
      break;
  }
  if (selected) {
    color = color.lighter(118);
  }
  return color;
}

QString format_us(std::uint64_t us) {
  if (us >= 1'000'000) {
    return QString::number(static_cast<double>(us) / 1'000'000.0, 'f', 2) + QStringLiteral(" s");
  }
  if (us >= 1000) {
    return QString::number(static_cast<double>(us) / 1000.0, 'f', 2) + QStringLiteral(" ms");
  }
  return QString::number(us) + QStringLiteral(" us");
}

QString category_label(const TimingTimelineWidget& widget, TimingCategory category) {
  switch (category) {
    case TimingCategory::Command:
      return widget.tr("Command");
    case TimingCategory::Modeling:
      return widget.tr("Modeling");
    case TimingCategory::Render:
      return widget.tr("Render");
    case TimingCategory::Ui:
      return widget.tr("UI");
  }
  return QStringLiteral("?");
}

std::uint64_t nice_tick(std::uint64_t span) {
  const double raw = static_cast<double>(span) / 8.0;
  if (raw <= 1000.0) {
    return 1000;
  }
  const double exp = std::pow(10.0, std::floor(std::log10(raw)));
  const double mantissa = raw / exp;
  double nice = 2.0;
  if (mantissa > 5.0) {
    nice = 10.0;
  } else if (mantissa > 2.0) {
    nice = 5.0;
  }
  return static_cast<std::uint64_t>(nice * exp);
}

bool is_ancestor(const std::vector<TimingEvent>& events, int ancestor, int node) {
  int parent = events[static_cast<std::size_t>(node)].parent;
  int guard = 0;
  while (parent >= 0 && parent < static_cast<int>(events.size()) && guard++ < 24) {
    if (parent == ancestor) {
      return true;
    }
    parent = events[static_cast<std::size_t>(parent)].parent;
  }
  return false;
}

}  // namespace

TimingTimelineWidget::TimingTimelineWidget(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(kRulerH + kLegendH + kRowH * 3 + kPad);
  setMouseTracking(true);
  setAutoFillBackground(true);
  setCursor(Qt::OpenHandCursor);
}

int TimingTimelineWidget::plot_top() const { return kRulerH + kLegendH; }

void TimingTimelineWidget::set_session(std::vector<TimingEvent> events, std::uint64_t duration_us) {
  const std::uint64_t old_duration = duration_us_;
  const bool was_fitted = view_start_us_ == 0 && view_span_us_ == old_duration;
  events_ = std::move(events);
  duration_us_ = std::max<std::uint64_t>(duration_us, 1);
  if (selected_ >= static_cast<int>(events_.size())) {
    selected_ = -1;
  }
  depths_.assign(events_.size(), 0);
  exclusive_ = exclusive_durations(events_);
  max_depth_ = 0;
  for (int i = 0; i < static_cast<int>(events_.size()); ++i) {
    depths_[static_cast<std::size_t>(i)] = event_depth(events_, i);
    max_depth_ = std::max(max_depth_, depths_[static_cast<std::size_t>(i)]);
  }
  setMinimumHeight(kRulerH + kLegendH + (max_depth_ + 1) * kRowH + kPad * 2);
  if (events_.empty() || was_fitted) {
    fit_view();
  } else {
    rebuild_hits();
    update();
  }
}

void TimingTimelineWidget::set_recording(bool recording) {
  if (recording_ == recording) {
    return;
  }
  recording_ = recording;
  update();
}

void TimingTimelineWidget::set_selected(int index) {
  selected_ = index;
  update();
}

void TimingTimelineWidget::fit_view() {
  view_start_us_ = 0;
  view_span_us_ = std::max<std::uint64_t>(duration_us_, 1);
  rebuild_hits();
  update();
}

void TimingTimelineWidget::zoom_to_event(int index) {
  if (index < 0 || index >= static_cast<int>(events_.size())) {
    fit_view();
    return;
  }
  const TimingEvent& event = events_[static_cast<std::size_t>(index)];
  const std::uint64_t dur = std::max<std::uint64_t>(event.duration_us, 1);
  const std::uint64_t pad = std::max<std::uint64_t>(dur / 10, 1);
  view_start_us_ = event.start_us > pad ? event.start_us - pad : 0;
  view_span_us_ = dur + pad * 2;
  rebuild_hits();
  update();
}

void TimingTimelineWidget::rebuild_hits() {
  hits_.clear();
  hits_.reserve(events_.size());
  for (int i = 0; i < static_cast<int>(events_.size()); ++i) {
    const TimingEvent& event = events_[static_cast<std::size_t>(i)];
    const int depth = i < static_cast<int>(depths_.size()) ? depths_[static_cast<std::size_t>(i)] : 0;
    const qreal x = us_to_x(event.start_us);
    const qreal x2 = us_to_x(event.start_us + std::max<std::uint64_t>(event.duration_us, 1));
    const qreal y = static_cast<qreal>(plot_top() + depth * kRowH + (kRowH - kBarH) / 2);
    hits_.push_back(Hit{i, depth, QRectF(x, y, std::max(4.0, x2 - x), kBarH)});
  }
}

qreal TimingTimelineWidget::us_to_x(std::uint64_t us) const {
  const qreal plot_w = std::max(1.0, static_cast<qreal>(width() - kPad * 2));
  const qreal rel = static_cast<qreal>(us) - static_cast<qreal>(view_start_us_);
  return static_cast<qreal>(kPad) + rel * plot_w / static_cast<qreal>(view_span_us_);
}

std::uint64_t TimingTimelineWidget::x_to_us(qreal x) const {
  const qreal plot_w = std::max(1.0, static_cast<qreal>(width() - kPad * 2));
  const qreal rel = (x - static_cast<qreal>(kPad)) / plot_w;
  const auto us = static_cast<double>(view_start_us_) + rel * static_cast<double>(view_span_us_);
  return us < 0 ? 0 : static_cast<std::uint64_t>(us);
}

int TimingTimelineWidget::hit_index(const QPoint& pos) const {
  int best = -1;
  int best_depth = -1;
  for (const Hit& hit : hits_) {
    if (!hit.rect.contains(pos)) {
      continue;
    }
    if (hit.depth >= best_depth) {
      best_depth = hit.depth;
      best = hit.index;
    }
  }
  return best;
}

void TimingTimelineWidget::paint_empty(QPainter& p) const {
  p.setPen(palette().color(QPalette::Mid));
  QFont title = font();
  title.setPointSize(title.pointSize() + 1);
  title.setBold(true);
  p.setFont(title);
  const QRect inner = rect().adjusted(24, 16, -24, -16);
  if (recording_) {
    p.drawText(inner, Qt::AlignCenter, tr("Recording — perform a command"));
    return;
  }
  p.drawText(inner.adjusted(0, 0, 0, -48), Qt::AlignHCenter | Qt::AlignVCenter,
             tr("No session yet"));
  p.setFont(font());
  p.drawText(inner.adjusted(0, 36, 0, 0), Qt::AlignHCenter | Qt::AlignVCenter,
             tr("Record, do some work, then stop.\n"
                "Nested calls stack downward like a flame graph.\n"
                "The call tree lists Total (with children) and Self (this scope only).\n"
                "Wheel zooms, drag pans, double-click a bar to focus."));
}

void TimingTimelineWidget::paint_ruler(QPainter& p) const {
  p.fillRect(QRect(0, 0, width(), kRulerH), palette().alternateBase());
  p.setPen(QPen(palette().color(QPalette::Mid), 1));
  p.drawLine(0, kRulerH - 1, width(), kRulerH - 1);

  const std::uint64_t tick = nice_tick(view_span_us_);
  const std::uint64_t first = (view_start_us_ / tick) * tick;
  QFont tick_font = font();
  tick_font.setPointSize(std::max(8, tick_font.pointSize() - 1));
  p.setFont(tick_font);
  for (std::uint64_t t = first; t <= view_start_us_ + view_span_us_ + tick; t += tick) {
    const qreal x = us_to_x(t);
    if (x < 2 || x > width() - 4) {
      continue;
    }
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(QPointF(x, kRulerH - 6), QPointF(x, kRulerH - 1));
    p.setPen(palette().color(QPalette::Text));
    p.drawText(QRectF(x + 3, 2, 72, kRulerH - 6), Qt::AlignLeft | Qt::AlignVCenter, format_us(t));
  }
}

void TimingTimelineWidget::paint_legend(QPainter& p) const {
  const int y = kRulerH;
  p.fillRect(QRect(0, y, width(), kLegendH), palette().base());
  struct Item {
    TimingCategory category;
    QString label;
  };
  const Item items[] = {
      {TimingCategory::Command, tr("Command")},
      {TimingCategory::Modeling, tr("Modeling")},
      {TimingCategory::Render, tr("Render")},
  };
  QFont legend_font = font();
  legend_font.setPointSize(std::max(8, legend_font.pointSize() - 1));
  p.setFont(legend_font);
  int x = kPad;
  for (const Item& item : items) {
    p.setBrush(category_color(item.category, false));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRect(x, y + 5, 10, 10), 2, 2);
    x += 14;
    p.setPen(palette().color(QPalette::Text));
    const QRect text(x, y, 80, kLegendH);
    p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, item.label);
    x += p.fontMetrics().horizontalAdvance(item.label) + 16;
  }
  p.setPen(palette().color(QPalette::Mid));
  p.drawText(QRect(x, y, width() - x - kPad, kLegendH), Qt::AlignRight | Qt::AlignVCenter,
             tr("Parent on top, children below"));
}

void TimingTimelineWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), palette().base());

  if (events_.empty()) {
    paint_empty(p);
    return;
  }

  paint_ruler(p);
  paint_legend(p);

  const int top = plot_top();
  for (int depth = 0; depth <= max_depth_; ++depth) {
    const int y = top + depth * kRowH;
    if (depth % 2 == 1) {
      p.fillRect(QRect(0, y, width(), kRowH), palette().alternateBase());
    }
  }

  p.setClipRect(QRect(0, top, width(), height() - top));
  for (const Hit& hit : hits_) {
    const TimingEvent& event = events_[static_cast<std::size_t>(hit.index)];
    const bool selected = hit.index == selected_;
    const bool related =
        selected_ >= 0 && (hit.index == selected_ || is_ancestor(events_, selected_, hit.index) ||
                           is_ancestor(events_, hit.index, selected_));
    QColor fill = category_color(event.category, selected);
    if (related && !selected) {
      fill = fill.lighter(130);
    }
    p.setBrush(fill);
    if (selected) {
      p.setPen(QPen(Qt::white, 1.4));
    } else if (related) {
      p.setPen(QPen(Qt::white, 1.0));
    } else {
      p.setPen(QPen(fill.darker(140), 0.8));
    }
    p.drawRoundedRect(hit.rect, 3, 3);
    if (hit.rect.width() > 28) {
      p.setPen(Qt::white);
      QString label = QString::fromStdString(event.name);
      if (hit.rect.width() > 72) {
        label += QStringLiteral("  ") + format_us(event.duration_us);
      }
      p.drawText(hit.rect.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    }
  }
}

void TimingTimelineWidget::wheelEvent(QWheelEvent* event) {
  if (events_.empty() || event->angleDelta().y() == 0 || width() <= kPad * 4) {
    return;
  }
  const double factor = event->angleDelta().y() > 0 ? 0.8 : 1.25;
  const std::uint64_t anchor = x_to_us(event->position().x());
  auto span = static_cast<double>(view_span_us_) * factor;
  span = std::clamp(span, 500.0, static_cast<double>(std::max(duration_us_, std::uint64_t{1})) * 4.0);
  const double left_ratio =
      (static_cast<double>(anchor) - static_cast<double>(view_start_us_)) /
      static_cast<double>(std::max<std::uint64_t>(view_span_us_, 1));
  view_span_us_ = static_cast<std::uint64_t>(span);
  const double start = static_cast<double>(anchor) - left_ratio * span;
  view_start_us_ = start < 0 ? 0 : static_cast<std::uint64_t>(start);
  rebuild_hits();
  update();
  event->accept();
}

void TimingTimelineWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton &&
      event->button() != Qt::RightButton) {
    return;
  }
  const int index = hit_index(event->pos());
  press_on_event_ = event->button() == Qt::LeftButton && index >= 0;
  panning_ = !press_on_event_;
  pan_last_ = event->pos();
  pan_start_view_us_ = view_start_us_;
  if (press_on_event_) {
    selected_ = index;
    update();
    emit event_clicked(index);
  } else {
    setCursor(Qt::ClosedHandCursor);
  }
}

void TimingTimelineWidget::mouseMoveEvent(QMouseEvent* event) {
  if (panning_ || (press_on_event_ && (event->pos() - pan_last_).manhattanLength() >= kPanSlop)) {
    if (press_on_event_) {
      press_on_event_ = false;
      panning_ = true;
      setCursor(Qt::ClosedHandCursor);
    }
    const qreal plot_w = std::max(1.0, static_cast<qreal>(width() - kPad * 2));
    const double live = static_cast<double>(pan_start_view_us_) -
                        static_cast<double>(event->pos().x() - pan_last_.x()) *
                            static_cast<double>(view_span_us_) / plot_w;
    view_start_us_ = live < 0 ? 0 : static_cast<std::uint64_t>(live);
    rebuild_hits();
    update();
    return;
  }
  const int index = hit_index(event->pos());
  setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::OpenHandCursor);
  if (index >= 0 && index < static_cast<int>(events_.size())) {
    const auto& e = events_[static_cast<std::size_t>(index)];
    QString parent;
    if (e.parent >= 0 && e.parent < static_cast<int>(events_.size())) {
      parent = QString::fromStdString(events_[static_cast<std::size_t>(e.parent)].name);
    }
    const std::uint64_t self =
        index < static_cast<int>(exclusive_.size()) ? exclusive_[static_cast<std::size_t>(index)]
                                                    : e.duration_us;
    QString text = QStringLiteral("%1\n%2\n%3  %4\n%5  %6")
                       .arg(QString::fromStdString(e.name), category_label(*this, e.category),
                            tr("Total"), format_us(e.duration_us), tr("Self"),
                            format_us(self));
    if (!parent.isEmpty()) {
      text += QStringLiteral("\n") + tr("inside %1").arg(parent);
    }
    QToolTip::showText(event->globalPosition().toPoint(), text, this);
  } else {
    QToolTip::hideText();
  }
}

void TimingTimelineWidget::mouseReleaseEvent(QMouseEvent*) {
  panning_ = false;
  press_on_event_ = false;
  setCursor(Qt::OpenHandCursor);
}

void TimingTimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  const int index = hit_index(event->pos());
  if (index >= 0) {
    zoom_to_event(index);
  } else {
    fit_view();
  }
}

void TimingTimelineWidget::resizeEvent(QResizeEvent*) { rebuild_hits(); }

}  // namespace tamias
