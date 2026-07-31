#pragma once

#include <QString>
#include <QWidget>

namespace tamias {

enum class ViewCubeFace {
  Front,
  Back,
  Left,
  Right,
  Top,
  Bottom,
};

// Small orientation cube overlay: drag to orbit, click a face to snap the view.
class ViewCubeWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit ViewCubeWidget(QWidget* parent = nullptr);

  void set_orientation(float yaw, float pitch);

 signals:
  void face_clicked(tamias::ViewCubeFace face);
  void orbit_dragged(float dyaw, float dpitch);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  struct Vec3f {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
  };
  struct Vec2f {
    float x = 0.f;
    float y = 0.f;
  };
  struct FaceGeom {
    ViewCubeFace id = ViewCubeFace::Front;
    Vec3f corners[4]{};
    Vec3f normal{};
    QString label;
  };

  void camera_basis(Vec3f& right, Vec3f& up, Vec3f& forward) const;
  [[nodiscard]] Vec3f to_view(Vec3f p) const;
  [[nodiscard]] Vec2f project(Vec3f view_p) const;
  [[nodiscard]] int hit_face(const QPoint& pos) const;
  void rebuild_faces();

  float yaw_ = -0.8f;
  float pitch_ = 0.5f;
  int hover_face_ = -1;
  int press_face_ = -1;
  QPoint last_mouse_{};
  QPoint press_mouse_{};
  bool dragging_ = false;
  FaceGeom faces_[6]{};
};

}  // namespace tamias
