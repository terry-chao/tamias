#pragma once

#include <QIcon>
#include <QWidget>

class QMenu;
class QToolButton;
class QVBoxLayout;

namespace tamias {

// Floating vertical button strip on the right of a document viewport.
class ViewportToolStrip final : public QWidget {
  Q_OBJECT
 public:
  explicit ViewportToolStrip(QWidget* parent = nullptr);

  void set_plan_view(bool plan);
  [[nodiscard]] QMenu* visibility_menu() const { return visibility_menu_; }
  [[nodiscard]] QMenu* floor_menu() const { return floor_menu_; }

 signals:
  void plan_view_toggled(bool plan);
  void frame_all_clicked();
  void visibility_menu_about_to_show();
  void floor_menu_about_to_show();

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  void apply_plate_mask();
  QToolButton* add_button(QVBoxLayout* layout, const QIcon& icon, const QString& tip);
  [[nodiscard]] QIcon load_icon(const QString& resource) const;

  QToolButton* plan_button_ = nullptr;
  QMenu* visibility_menu_ = nullptr;
  QMenu* floor_menu_ = nullptr;
  QIcon icon_2d_;
  QIcon icon_3d_;
};

}  // namespace tamias
