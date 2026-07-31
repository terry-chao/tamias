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

// Eight cube corners as sign combinations of (+/-X, +/-Y, +/-Z).
// Front=+Z, Right=+X, Top=+Y.
enum class ViewCubeCorner {
  RightTopFront,     // +X +Y +Z
  LeftTopFront,      // -X +Y +Z
  RightTopBack,      // +X +Y -Z
  LeftTopBack,       // -X +Y -Z
  RightBottomFront,  // +X -Y +Z
  LeftBottomFront,   // -X -Y +Z
  RightBottomBack,   // +X -Y -Z
  LeftBottomBack,    // -X -Y -Z
};

// Small orientation cube overlay: drag to orbit, click a face/corner to snap.
class ViewCubeWidget final : public QWidget {
  Q_OBJECT
 public:
  explicit ViewCubeWidget(QWidget* parent = nullptr);

  void set_orientation(float yaw, float pitch);

 signals:
  void face_clicked(tamias::ViewCubeFace face);
  void corner_clicked(tamias::ViewCubeCorner corner);
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
  struct CornerGeom {
    ViewCubeCorner id = ViewCubeCorner::RightTopFront;
    // Chamfer triangle: points inset along the three edges from the vertex.
    Vec3f pts[3]{};
    Vec3f vertex{};
    Vec3f normal{};  // outward diagonal
  };

  void camera_basis(Vec3f& right, Vec3f& up, Vec3f& forward) const;
  [[nodiscard]] Vec3f to_view(Vec3f p) const;
  [[nodiscard]] Vec2f project(Vec3f view_p) const;
  [[nodiscard]] int hit_face(const QPoint& pos) const;
  [[nodiscard]] int hit_corner(const QPoint& pos) const;
  void update_hover(const QPoint& pos);
  void rebuild_faces();
  void rebuild_corners();

  float yaw_ = 0.785398163f;
  float pitch_ = 0.35f;
  int hover_face_ = -1;
  int hover_corner_ = -1;
  int press_face_ = -1;
  int press_corner_ = -1;
  QPoint last_mouse_{};
  QPoint press_mouse_{};
  bool dragging_ = false;
  FaceGeom faces_[6]{};
  CornerGeom corners_[8]{};
};

}  // namespace tamias
