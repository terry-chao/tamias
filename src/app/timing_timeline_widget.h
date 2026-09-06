#pragma once

#include "engine/profile/timing_event.h"

#include <QWidget>
#include <cstdint>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QWheelEvent;

namespace tamias {

class TimingTimelineWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit TimingTimelineWidget(QWidget* parent = nullptr);

  void set_session(std::vector<TimingEvent> events, std::uint64_t duration_us);
  void set_recording(bool recording);
  void set_selected(int index);
  void fit_view();
  [[nodiscard]] int selected() const { return selected_; }

 signals:
  void event_clicked(int index);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  struct Hit {
    int index = -1;
    int depth = 0;
    QRectF rect;
  };

  void rebuild_hits();
  void zoom_to_event(int index);
  [[nodiscard]] int hit_index(const QPoint& pos) const;
  [[nodiscard]] qreal us_to_x(std::uint64_t us) const;
  [[nodiscard]] std::uint64_t x_to_us(qreal x) const;
  [[nodiscard]] int plot_top() const;
  void paint_empty(QPainter& p) const;
  void paint_ruler(QPainter& p) const;
  void paint_legend(QPainter& p) const;

  std::vector<TimingEvent> events_;
  std::vector<int> depths_;
  std::vector<std::uint64_t> exclusive_;
  int max_depth_ = 0;
  std::uint64_t duration_us_ = 1;
  std::uint64_t view_start_us_ = 0;
  std::uint64_t view_span_us_ = 1;
  int selected_ = -1;
  bool recording_ = false;
  bool panning_ = false;
  bool press_on_event_ = false;
  QPoint pan_last_;
  std::uint64_t pan_start_view_us_ = 0;
  std::vector<Hit> hits_;
};

}  // namespace tamias
