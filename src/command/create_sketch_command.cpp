#include "create_sketch_command.h"

#include "entity/arc_entity.h"
#include "entity/bezier_entity.h"
#include "entity/bspline_entity.h"
#include "entity/circle_entity.h"
#include "entity/line_entity.h"
#include "entity/polyline_entity.h"
#include "entity/rectangle_entity.h"
#include "engine/modeling/curve_geom.h"

namespace tamias {
namespace {

bool nearly_same(Vec3 a, Vec3 b) { return length(a - b) < 1e-4f; }

}  // namespace

CreateSketchCommand::CreateSketchCommand(Document& document, SketchKind kind)
    : document_(&document), kind_(kind) {}

bool CreateSketchCommand::open_ended() const {
  switch (kind_) {
    case SketchKind::Polyline:
    case SketchKind::Bezier:
    case SketchKind::BSpline:
      return true;
    default:
      return false;
  }
}

bool CreateSketchCommand::shows_control_polygon() const {
  switch (kind_) {
    case SketchKind::Bezier:
    case SketchKind::BSpline:
      return true;
    default:
      return false;
  }
}

int CreateSketchCommand::required_points() const {
  switch (kind_) {
    case SketchKind::Arc:
      return 3;
    case SketchKind::Polyline:
    case SketchKind::Bezier:
    case SketchKind::BSpline:
      return 0;  // 不定长，靠 confirm（右键 / Enter）
    case SketchKind::Line:
    case SketchKind::Circle:
    case SketchKind::Rectangle:
    default:
      return 2;
  }
}

Result<bool> CreateSketchCommand::on_point(Vec3 point) {
  if (!points_.empty() && nearly_same(points_.back(), point)) {
    return false;
  }
  points_.push_back(point);
  if (open_ended()) {
    return false;
  }
  return static_cast<int>(points_.size()) >= required_points();
}

Result<bool> CreateSketchCommand::on_confirm() {
  if (!open_ended()) {
    return false;
  }
  if (points_.size() < 2) {
    return false;
  }
  return true;
}

std::vector<Vec3> CreateSketchCommand::live_controls(Vec3 cursor) const {
  if (points_.empty()) {
    return {};
  }
  std::vector<Vec3> pts = points_;
  if (!nearly_same(pts.back(), cursor)) {
    pts.push_back(cursor);
  }
  return pts;
}

std::vector<Vec3> CreateSketchCommand::preview_control_polyline(Vec3 cursor) const {
  if (!shows_control_polygon()) {
    return {};
  }
  return live_controls(cursor);
}

std::vector<Vec3> CreateSketchCommand::preview_points(Vec3 cursor) const {
  if (!shows_control_polygon()) {
    return {};
  }
  return live_controls(cursor);
}

std::vector<Vec3> CreateSketchCommand::preview_polyline(Vec3 cursor) const {
  if (points_.empty()) {
    return {};
  }
  switch (kind_) {
    case SketchKind::Circle: {
      const float radius = length(cursor - points_[0]);
      if (radius < 1e-4f) {
        return {};
      }
      return sample_circle_xz(points_[0], radius);
    }
    case SketchKind::Rectangle:
      if (nearly_same(points_[0], cursor)) {
        return {};
      }
      return sample_rect_xz(points_[0], cursor);
    case SketchKind::Arc:
      if (points_.size() == 1) {
        return {points_[0], cursor};
      }
      return sample_arc_3pt(points_[0], points_[1], cursor);
    case SketchKind::Bezier: {
      const std::vector<Vec3> ctrls = live_controls(cursor);
      if (ctrls.size() < 2) {
        return {};
      }
      return sample_bezier(ctrls);
    }
    case SketchKind::BSpline: {
      const std::vector<Vec3> ctrls = live_controls(cursor);
      if (ctrls.size() < 2) {
        return {};
      }
      return sample_bspline(ctrls);
    }
    case SketchKind::Polyline: {
      std::vector<Vec3> pts = points_;
      if (!nearly_same(pts.back(), cursor)) {
        pts.push_back(cursor);
      }
      return pts;
    }
    case SketchKind::Line:
    default:
      return {points_[0], cursor};
  }
}

Result<std::unique_ptr<Entity>> CreateSketchCommand::make_sketch() const {
  switch (kind_) {
    case SketchKind::Line:
      if (points_.size() < 2 || nearly_same(points_[0], points_[1])) {
        return Err("Line needs two distinct points");
      }
      return std::make_unique<LineEntity>(points_[0], points_[1]);
    case SketchKind::Polyline:
      if (points_.size() < 2) {
        return Err("Polyline needs at least two points");
      }
      return std::make_unique<PolylineEntity>(points_);
    case SketchKind::Circle: {
      if (points_.size() < 2) {
        return Err("Circle needs a center and a radius point");
      }
      const double radius = static_cast<double>(length(points_[1] - points_[0]));
      if (radius < 1e-4) {
        return Err("Circle radius is too small");
      }
      return std::make_unique<CircleEntity>(points_[0], radius);
    }
    case SketchKind::Arc:
      if (points_.size() < 3) {
        return Err("Arc needs start, through, and end points");
      }
      return std::make_unique<ArcEntity>(points_[0], points_[1], points_[2]);
    case SketchKind::Bezier:
      if (points_.size() < 2) {
        return Err("Bezier needs at least two control points");
      }
      return std::make_unique<BezierEntity>(points_);
    case SketchKind::BSpline:
      if (points_.size() < 2) {
        return Err("B-spline needs at least two control points");
      }
      return std::make_unique<BSplineEntity>(points_);
    case SketchKind::Rectangle:
      if (points_.size() < 2 || nearly_same(points_[0], points_[1])) {
        return Err("Rectangle needs two distinct corners");
      }
      return std::make_unique<RectangleEntity>(points_[0], points_[1]);
  }
  return Err("unknown sketch kind");
}

Result<void> CreateSketchCommand::execute() {
  auto built = make_sketch();
  if (!built) {
    return Err(built.error());
  }
  auto geometry = (*built)->createGeom();
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::move(*built), std::move(*geometry));
  if (added == nullptr) {
    return Err("CreateSketchCommand: add entity failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateSketchCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateSketchCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
