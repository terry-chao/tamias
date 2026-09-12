#pragma once

#include <QPointF>
#include <QTransform>
#include <QWidget>

#include <memory>

class QKeyEvent;
class QMouseEvent;
class QContextMenuEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace tamias {

class DrawingDocument;

// 二维图纸视图：滚轮缩放、按住左键拖拽平移、翻页、图层开关。
// 只做"看"，不编辑图元；底图相关的对齐/联动属于后续工作。
class DrawingView final : public QWidget {
  Q_OBJECT
 public:
  explicit DrawingView(std::unique_ptr<DrawingDocument> document, QWidget* parent = nullptr);
  ~DrawingView() override;

  [[nodiscard]] const DrawingDocument& document() const { return *document_; }
  [[nodiscard]] DrawingDocument& document() { return *document_; }
  [[nodiscard]] QString title() const;

  void fit_to_window();
  void zoom_in();
  void zoom_out();
  void reset_zoom();
  [[nodiscard]] double zoom() const { return zoom_; }

  void set_page(int page);
  [[nodiscard]] int page() const { return page_; }

  void toggle_background();
  [[nodiscard]] bool dark_background() const;
  void set_layer_visible(int index, bool visible);
  [[nodiscard]] bool layer_visible(int index) const;

 signals:
  void status_message(const QString& text);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  [[nodiscard]] QTransform world_to_device() const;
  [[nodiscard]] QPointF to_world(const QPointF& device) const;
  void zoom_by(double factor, const QPointF& anchor);
  void center_page();
  void update_status();

  std::unique_ptr<DrawingDocument> document_;
  int page_ = 0;
  double zoom_ = 1.0;
  QPointF center_{};
  bool panning_ = false;
  QPointF last_drag_{};
  bool has_cursor_ = false;
  QPointF cursor_world_{};
  // 用户自己调过缩放/平移后，窗口尺寸变化不再自动重适配。
  bool user_adjusted_ = false;
};

}  // namespace tamias
