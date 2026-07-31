#include "view_cube_widget.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <vector>

namespace tamias {
namespace {

constexpr float kHalf = 0.55f;
constexpr float kCornerInset = 0.22f;  // world units along each edge from vertex

QColor face_color(ViewCubeFace face, bool hovered) {
  QColor c;
  switch (face) {
    case ViewCubeFace::Top:
      c = QColor(70, 130, 200);
      break;
    case ViewCubeFace::Bottom:
      c = QColor(90, 100, 120);
      break;
    case ViewCubeFace::Front:
      c = QColor(80, 160, 110);
      break;
    case ViewCubeFace::Back:
      c = QColor(70, 140, 100);
      break;
    case ViewCubeFace::Right:
      c = QColor(200, 110, 90);
      break;
    case ViewCubeFace::Left:
      c = QColor(180, 100, 85);
      break;
  }
  if (hovered) {
    c = c.lighter(130);
  }
  return c;
}

bool point_in_quad(const QPointF& p, const QPointF q[4]) {
  auto cross = [](QPointF a, QPointF b, QPointF c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
  };
  bool has_neg = false;
  bool has_pos = false;
  for (int i = 0; i < 4; ++i) {
    const float z = static_cast<float>(cross(q[i], q[(i + 1) % 4], p));
    if (z < 0.f) {
      has_neg = true;
    } else if (z > 0.f) {
      has_pos = true;
    }
    if (has_neg && has_pos) {
      return false;
    }
  }
  return true;
}

bool point_in_triangle(const QPointF& p, const QPointF t[3]) {
  auto cross = [](QPointF a, QPointF b, QPointF c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
  };
  const float z0 = static_cast<float>(cross(t[0], t[1], p));
  const float z1 = static_cast<float>(cross(t[1], t[2], p));
  const float z2 = static_cast<float>(cross(t[2], t[0], p));
  const bool has_neg = (z0 < 0.f) || (z1 < 0.f) || (z2 < 0.f);
  const bool has_pos = (z0 > 0.f) || (z1 > 0.f) || (z2 > 0.f);
  return !(has_neg && has_pos);
}

}  // namespace

ViewCubeWidget::ViewCubeWidget(QWidget* parent) : QWidget(parent) {
  setFixedSize(96, 96);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  // Native HWND so this overlay stacks above the Vulkan surface child on Win32.
  // Match the viewport clear gray so rounded-plate corners never flash black.
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(31, 33, 38));
  setPalette(pal);
  setToolTip(tr("Drag to orbit · Click a face or corner to snap the view"));
  rebuild_faces();
  rebuild_corners();
}

void ViewCubeWidget::set_orientation(float yaw, float pitch) {
  if (std::abs(yaw_ - yaw) < 1e-4f && std::abs(pitch_ - pitch) < 1e-4f) {
    return;
  }
  yaw_ = yaw;
  pitch_ = pitch;
  update();
}

void ViewCubeWidget::rebuild_faces() {
  // Face corners in world space (Y-up). Order is CCW when viewed from outside.
  // Front=+Z, Back=-Z, Right=+X, Left=-X, Top=+Y, Bottom=-Y.
  faces_[0] = {ViewCubeFace::Front,
               {{-kHalf, -kHalf, kHalf},
                {kHalf, -kHalf, kHalf},
                {kHalf, kHalf, kHalf},
                {-kHalf, kHalf, kHalf}},
               {0.f, 0.f, 1.f},
               tr("Front")};
  faces_[1] = {ViewCubeFace::Back,
               {{kHalf, -kHalf, -kHalf},
                {-kHalf, -kHalf, -kHalf},
                {-kHalf, kHalf, -kHalf},
                {kHalf, kHalf, -kHalf}},
               {0.f, 0.f, -1.f},
               tr("Back")};
  faces_[2] = {ViewCubeFace::Left,
               {{-kHalf, -kHalf, -kHalf},
                {-kHalf, -kHalf, kHalf},
                {-kHalf, kHalf, kHalf},
                {-kHalf, kHalf, -kHalf}},
               {-1.f, 0.f, 0.f},
               tr("Left")};
  faces_[3] = {ViewCubeFace::Right,
               {{kHalf, -kHalf, kHalf},
                {kHalf, -kHalf, -kHalf},
                {kHalf, kHalf, -kHalf},
                {kHalf, kHalf, kHalf}},
               {1.f, 0.f, 0.f},
               tr("Right")};
  faces_[4] = {ViewCubeFace::Top,
               {{-kHalf, kHalf, kHalf},
                {kHalf, kHalf, kHalf},
                {kHalf, kHalf, -kHalf},
                {-kHalf, kHalf, -kHalf}},
               {0.f, 1.f, 0.f},
               tr("Top")};
  faces_[5] = {ViewCubeFace::Bottom,
               {{-kHalf, -kHalf, -kHalf},
                {kHalf, -kHalf, -kHalf},
                {kHalf, -kHalf, kHalf},
                {-kHalf, -kHalf, kHalf}},
               {0.f, -1.f, 0.f},
               tr("Bottom")};
}

void ViewCubeWidget::rebuild_corners() {
  // Sign triples: (sx, sy, sz) for Right/Left × Top/Bottom × Front/Back.
  struct Spec {
    ViewCubeCorner id;
    float sx;
    float sy;
    float sz;
  };
  const Spec specs[8] = {
      {ViewCubeCorner::RightTopFront, 1.f, 1.f, 1.f},
      {ViewCubeCorner::LeftTopFront, -1.f, 1.f, 1.f},
      {ViewCubeCorner::RightTopBack, 1.f, 1.f, -1.f},
      {ViewCubeCorner::LeftTopBack, -1.f, 1.f, -1.f},
      {ViewCubeCorner::RightBottomFront, 1.f, -1.f, 1.f},
      {ViewCubeCorner::LeftBottomFront, -1.f, -1.f, 1.f},
      {ViewCubeCorner::RightBottomBack, 1.f, -1.f, -1.f},
      {ViewCubeCorner::LeftBottomBack, -1.f, -1.f, -1.f},
  };

  for (int i = 0; i < 8; ++i) {
    const auto& s = specs[i];
    CornerGeom& c = corners_[i];
    c.id = s.id;
    c.vertex = {s.sx * kHalf, s.sy * kHalf, s.sz * kHalf};
    // Inset along each axis toward the cube center → chamfer triangle.
    c.pts[0] = {s.sx * (kHalf - kCornerInset), s.sy * kHalf, s.sz * kHalf};
    c.pts[1] = {s.sx * kHalf, s.sy * (kHalf - kCornerInset), s.sz * kHalf};
    c.pts[2] = {s.sx * kHalf, s.sy * kHalf, s.sz * (kHalf - kCornerInset)};
    const float inv = 1.f / std::sqrt(3.f);
    c.normal = {s.sx * inv, s.sy * inv, s.sz * inv};
  }
}

void ViewCubeWidget::camera_basis(Vec3f& right, Vec3f& up, Vec3f& forward) const {
  // Same eye offset convention as TurntableCamera (Y-up).
  const float cp = std::cos(pitch_);
  const float sp = std::sin(pitch_);
  const float cy = std::cos(yaw_);
  const float sy = std::sin(yaw_);
  // Direction from target toward eye.
  const Vec3f eye_dir{cp * sy, sp, cp * cy};
  forward = {-eye_dir.x, -eye_dir.y, -eye_dir.z};  // look direction
  const Vec3f world_up{0.f, 1.f, 0.f};
  // right = normalize(cross(forward, world_up))
  right = {forward.y * world_up.z - forward.z * world_up.y,
           forward.z * world_up.x - forward.x * world_up.z,
           forward.x * world_up.y - forward.y * world_up.x};
  float len = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
  if (len < 1e-5f) {
    right = {1.f, 0.f, 0.f};
  } else {
    right = {right.x / len, right.y / len, right.z / len};
  }
  // up = cross(right, forward)
  up = {right.y * forward.z - right.z * forward.y, right.z * forward.x - right.x * forward.z,
        right.x * forward.y - right.y * forward.x};
}

ViewCubeWidget::Vec3f ViewCubeWidget::to_view(Vec3f p) const {
  Vec3f right, up, forward;
  camera_basis(right, up, forward);
  // View-space: x along right, y along up, z along look (more positive = farther).
  return {p.x * right.x + p.y * right.y + p.z * right.z,
          p.x * up.x + p.y * up.y + p.z * up.z,
          -(p.x * forward.x + p.y * forward.y + p.z * forward.z)};
}

ViewCubeWidget::Vec2f ViewCubeWidget::project(Vec3f view_p) const {
  const float scale = static_cast<float>(width()) * 0.52f;
  const float cx = static_cast<float>(width()) * 0.5f;
  const float cy = static_cast<float>(height()) * 0.5f;
  return {cx + view_p.x * scale, cy - view_p.y * scale};
}

int ViewCubeWidget::hit_face(const QPoint& pos) const {
  struct Candidate {
    int index = -1;
    float depth = 0.f;
  };
  Candidate best{-1, 1e9f};
  const QPointF p(pos);

  Vec3f right, up, forward;
  camera_basis(right, up, forward);
  const Vec3f eye_dir{-forward.x, -forward.y, -forward.z};

  for (int i = 0; i < 6; ++i) {
    const auto& face = faces_[i];
    const float facing =
        face.normal.x * eye_dir.x + face.normal.y * eye_dir.y + face.normal.z * eye_dir.z;
    if (facing < 0.05f) {
      continue;
    }

    QPointF q[4];
    float depth = 0.f;
    for (int c = 0; c < 4; ++c) {
      const Vec3f v = to_view(face.corners[c]);
      const auto pr = project(v);
      q[c] = QPointF(pr.x, pr.y);
      depth += v.z;
    }
    depth *= 0.25f;

    if (point_in_quad(p, q) && depth < best.depth) {
      best = {i, depth};
    }
  }
  return best.index;
}

int ViewCubeWidget::hit_corner(const QPoint& pos) const {
  struct Candidate {
    int index = -1;
    float depth = 0.f;
  };
  Candidate best{-1, 1e9f};
  const QPointF p(pos);

  Vec3f right, up, forward;
  camera_basis(right, up, forward);
  const Vec3f eye_dir{-forward.x, -forward.y, -forward.z};

  for (int i = 0; i < 8; ++i) {
    const auto& corner = corners_[i];
    const float facing =
        corner.normal.x * eye_dir.x + corner.normal.y * eye_dir.y + corner.normal.z * eye_dir.z;
    if (facing < 0.08f) {
      continue;
    }

    QPointF t[3];
    float depth = 0.f;
    for (int k = 0; k < 3; ++k) {
      const Vec3f v = to_view(corner.pts[k]);
      const auto pr = project(v);
      t[k] = QPointF(pr.x, pr.y);
      depth += v.z;
    }
    depth *= (1.f / 3.f);

    // Prefer triangle hit; also accept a small radius around the vertex so thin
    // foreshortened corners remain clickable.
    const Vec3f vv = to_view(corner.vertex);
    const Vec2f vp = project(vv);
    const float dx = static_cast<float>(p.x() - vp.x);
    const float dy = static_cast<float>(p.y() - vp.y);
    const bool near_vertex = (dx * dx + dy * dy) <= (10.f * 10.f);

    if ((point_in_triangle(p, t) || near_vertex) && depth < best.depth) {
      best = {i, depth};
    }
  }
  return best.index;
}

void ViewCubeWidget::update_hover(const QPoint& pos) {
  const int corner = hit_corner(pos);
  int face = -1;
  if (corner < 0) {
    face = hit_face(pos);
  }
  if (corner != hover_corner_ || face != hover_face_) {
    hover_corner_ = corner;
    hover_face_ = face;
    update();
  }
}

void ViewCubeWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Fill the full HWND rect first — outside a rounded plate would otherwise stay black.
  const QColor bg(31, 33, 38);
  painter.fillRect(rect(), bg);

  QPainterPath plate;
  plate.addRoundedRect(QRectF(rect()).adjusted(3, 3, -3, -3), 10, 10);
  painter.fillPath(plate, QColor(42, 45, 52));
  painter.setPen(QPen(QColor(255, 255, 255, 36), 1.0));
  painter.drawPath(plate);

  struct DrawFace {
    int index = 0;
    float depth = 0.f;
    QPointF pts[4];
    QPointF center;
  };
  struct DrawCorner {
    int index = 0;
    float depth = 0.f;
    QPointF pts[3];
  };

  std::vector<DrawFace> visible_faces;
  visible_faces.reserve(6);
  std::vector<DrawCorner> visible_corners;
  visible_corners.reserve(8);

  Vec3f right, up, forward;
  camera_basis(right, up, forward);
  const Vec3f eye_dir{-forward.x, -forward.y, -forward.z};

  for (int i = 0; i < 6; ++i) {
    const auto& face = faces_[i];
    const float facing =
        face.normal.x * eye_dir.x + face.normal.y * eye_dir.y + face.normal.z * eye_dir.z;
    if (facing < 0.02f) {
      continue;
    }

    DrawFace df;
    df.index = i;
    float depth = 0.f;
    QPointF sum;
    for (int c = 0; c < 4; ++c) {
      const Vec3f v = to_view(face.corners[c]);
      const Vec2f pr = project(v);
      df.pts[c] = QPointF(pr.x, pr.y);
      sum += df.pts[c];
      depth += v.z;
    }
    df.depth = depth * 0.25f;
    df.center = sum * 0.25;
    visible_faces.push_back(df);
  }

  for (int i = 0; i < 8; ++i) {
    const auto& corner = corners_[i];
    const float facing =
        corner.normal.x * eye_dir.x + corner.normal.y * eye_dir.y + corner.normal.z * eye_dir.z;
    if (facing < 0.05f) {
      continue;
    }

    DrawCorner dc;
    dc.index = i;
    float depth = 0.f;
    for (int k = 0; k < 3; ++k) {
      const Vec3f v = to_view(corner.pts[k]);
      const Vec2f pr = project(v);
      dc.pts[k] = QPointF(pr.x, pr.y);
      depth += v.z;
    }
    dc.depth = depth * (1.f / 3.f);
    visible_corners.push_back(dc);
  }

  std::sort(visible_faces.begin(), visible_faces.end(),
            [](const DrawFace& a, const DrawFace& b) { return a.depth > b.depth; });
  std::sort(visible_corners.begin(), visible_corners.end(),
            [](const DrawCorner& a, const DrawCorner& b) { return a.depth > b.depth; });

  QFont font = painter.font();
  font.setBold(true);
  font.setPixelSize(13);
  painter.setFont(font);

  for (const auto& df : visible_faces) {
    const auto& face = faces_[df.index];
    QPainterPath path;
    path.moveTo(df.pts[0]);
    for (int c = 1; c < 4; ++c) {
      path.lineTo(df.pts[c]);
    }
    path.closeSubpath();

    const bool hovered = hover_corner_ < 0 && df.index == hover_face_;
    painter.fillPath(path, face_color(face.id, hovered));
    painter.setPen(QPen(QColor(255, 255, 255, hovered ? 220 : 140), hovered ? 1.6 : 1.0));
    painter.drawPath(path);

    painter.setPen(QColor(255, 255, 255, 230));
    painter.drawText(QRectF(df.center.x() - 12, df.center.y() - 10, 24, 20), Qt::AlignCenter,
                     face.label);
  }

  // Corner chamfers on top so they remain visible/clickable targets.
  for (const auto& dc : visible_corners) {
    QPainterPath path;
    path.moveTo(dc.pts[0]);
    path.lineTo(dc.pts[1]);
    path.lineTo(dc.pts[2]);
    path.closeSubpath();

    const bool hovered = dc.index == hover_corner_;
    QColor fill = hovered ? QColor(230, 230, 235) : QColor(200, 200, 210, 210);
    painter.fillPath(path, fill);
    painter.setPen(QPen(QColor(40, 42, 48, hovered ? 220 : 160), hovered ? 1.4 : 1.0));
    painter.drawPath(path);
  }
}

void ViewCubeWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  last_mouse_ = event->pos();
  press_mouse_ = event->pos();
  press_corner_ = hit_corner(event->pos());
  press_face_ = press_corner_ >= 0 ? -1 : hit_face(event->pos());
  dragging_ = false;
  grabMouse();
}

void ViewCubeWidget::mouseMoveEvent(QMouseEvent* event) {
  if (event->buttons() & Qt::LeftButton) {
    const QPoint delta = event->pos() - last_mouse_;
    last_mouse_ = event->pos();
    if (!dragging_ && (event->pos() - press_mouse_).manhattanLength() >= 4) {
      dragging_ = true;
      hover_face_ = -1;
      hover_corner_ = -1;
    }
    if (dragging_ && (delta.x() != 0 || delta.y() != 0)) {
      // Match viewport orbit feel (left drag).
      emit orbit_dragged(-delta.x() * 0.02f, delta.y() * 0.02f);
    }
    return;
  }

  update_hover(event->pos());
}

void ViewCubeWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  releaseMouse();
  if (!dragging_) {
    if (press_corner_ >= 0) {
      emit corner_clicked(corners_[press_corner_].id);
    } else if (press_face_ >= 0) {
      emit face_clicked(faces_[press_face_].id);
    }
  }
  dragging_ = false;
  press_face_ = -1;
  press_corner_ = -1;
  update_hover(event->pos());
}

void ViewCubeWidget::leaveEvent(QEvent*) {
  if (!dragging_ && (hover_face_ != -1 || hover_corner_ != -1)) {
    hover_face_ = -1;
    hover_corner_ = -1;
    update();
  }
}

}  // namespace tamias
