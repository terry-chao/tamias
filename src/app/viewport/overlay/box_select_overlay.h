#pragma once

#include <QRect>
#include <QWidget>

namespace tamias {

// CAD-style box-select overlay: 1px white wireframe only (no fill).
class BoxSelectOverlay final : public QWidget {
 public:
  explicit BoxSelectOverlay(QWidget* parent = nullptr);

  void set_box(const QRect& rect, bool crossing);
  void hide_box();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private:
  void apply_wire_mask();
};

}  // namespace tamias
