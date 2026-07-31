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
  setToolTip(tr("Drag to orbit · Click a face: Front/Back/Left/Right/Top/Bottom"));
  rebuild_faces();
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
  std::vector<DrawFace> visible;
  visible.reserve(6);

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
    visible.push_back(df);
  }

  std::sort(visible.begin(), visible.end(),
            [](const DrawFace& a, const DrawFace& b) { return a.depth > b.depth; });

  QFont font = painter.font();
  font.setBold(true);
  font.setPixelSize(13);
  painter.setFont(font);

  for (const auto& df : visible) {
    const auto& face = faces_[df.index];
    QPainterPath path;
    path.moveTo(df.pts[0]);
    for (int c = 1; c < 4; ++c) {
      path.lineTo(df.pts[c]);
    }
    path.closeSubpath();

    const bool hovered = df.index == hover_face_;
    painter.fillPath(path, face_color(face.id, hovered));
    painter.setPen(QPen(QColor(255, 255, 255, hovered ? 220 : 140), hovered ? 1.6 : 1.0));
    painter.drawPath(path);

    painter.setPen(QColor(255, 255, 255, 230));
    painter.drawText(QRectF(df.center.x() - 12, df.center.y() - 10, 24, 20), Qt::AlignCenter,
                     face.label);
  }
}

void ViewCubeWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  last_mouse_ = event->pos();
  press_mouse_ = event->pos();
  press_face_ = hit_face(event->pos());
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
    }
    if (dragging_ && (delta.x() != 0 || delta.y() != 0)) {
      // Match viewport orbit feel (left drag).
      emit orbit_dragged(-delta.x() * 0.02f, delta.y() * 0.02f);
    }
    return;
  }

  const int hit = hit_face(event->pos());
  if (hit != hover_face_) {
    hover_face_ = hit;
    update();
  }
}

void ViewCubeWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  releaseMouse();
  if (!dragging_ && press_face_ >= 0) {
    emit face_clicked(faces_[press_face_].id);
  }
  dragging_ = false;
  press_face_ = -1;
  hover_face_ = hit_face(event->pos());
  update();
}

void ViewCubeWidget::leaveEvent(QEvent*) {
  if (!dragging_ && hover_face_ != -1) {
    hover_face_ = -1;
    update();
  }
}

}  // namespace tamias
