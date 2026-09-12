#include "entity_kind_catalog.h"

#include <QCoreApplication>

namespace tamias {
namespace {

// 用显式 context 而不是 tr()：这份表要能被 lupdate 扫到，同时不受调用方类名影响。
QString kind_label(const char* source) {
  return QCoreApplication::translate("tamias::EntityKindCatalog", source);
}

}  // namespace

const std::vector<EntityKindEntry>& entity_kind_catalog() {
  static const std::vector<EntityKindEntry> catalog = {
      // 建筑
      {EntityKind::Wall, ":/icons/wall.svg"},
      {EntityKind::Door, ":/icons/door.svg"},
      {EntityKind::Window, ":/icons/window.svg"},
      {EntityKind::CurtainWall, ":/icons/curtain_wall.svg"},
      // 结构
      {EntityKind::Column, ":/icons/column.svg"},
      {EntityKind::Beam, ":/icons/beam.svg"},
      {EntityKind::Slab, ":/icons/slab.svg"},
      {EntityKind::StructuralWall, ":/icons/structural_wall.svg"},
      {EntityKind::Foundation, ":/icons/foundation.svg"},
      // 草图与基础体
      {EntityKind::Box, ":/icons/box.svg"},
      {EntityKind::Cylinder, ":/icons/cylinder.svg"},
      {EntityKind::Line, ":/icons/line.svg"},
      {EntityKind::Polyline, ":/icons/polyline.svg"},
      {EntityKind::Circle, ":/icons/circle.svg"},
      {EntityKind::Arc, ":/icons/arc.svg"},
      {EntityKind::Rectangle, ":/icons/rectangle.svg"},
      {EntityKind::Bezier, ":/icons/bezier.svg"},
      {EntityKind::BSpline, ":/icons/bspline.svg"},
      {EntityKind::Nurbs, ":/icons/nurbs.svg"},
  };
  return catalog;
}

QString entity_kind_label(EntityKind kind) {
  switch (kind) {
    case EntityKind::Wall:
      return kind_label("Walls");
    case EntityKind::Door:
      return kind_label("Doors");
    case EntityKind::Window:
      return kind_label("Windows");
    case EntityKind::CurtainWall:
      return kind_label("Curtain Walls");
    case EntityKind::Column:
      return kind_label("Columns");
    case EntityKind::Beam:
      return kind_label("Beams");
    case EntityKind::Slab:
      return kind_label("Slabs");
    case EntityKind::StructuralWall:
      return kind_label("Structural Walls");
    case EntityKind::Foundation:
      return kind_label("Foundations");
    case EntityKind::Box:
      return kind_label("Boxes");
    case EntityKind::Cylinder:
      return kind_label("Cylinders");
    case EntityKind::Line:
      return kind_label("Lines");
    case EntityKind::Polyline:
      return kind_label("Polylines");
    case EntityKind::Circle:
      return kind_label("Circles");
    case EntityKind::Arc:
      return kind_label("Arcs");
    case EntityKind::Rectangle:
      return kind_label("Rectangles");
    case EntityKind::Bezier:
      return kind_label("Beziers");
    case EntityKind::BSpline:
      return kind_label("B-splines");
    case EntityKind::Nurbs:
      return kind_label("NURBS");
  }
  return kind_label("Unknown");
}

QString discipline_label(Discipline discipline) {
  switch (discipline) {
    case Discipline::Architectural:
      return kind_label("Architectural");
    case Discipline::Structural:
      return kind_label("Structural");
    case Discipline::None:
      break;
  }
  return kind_label("Sketch & Solids");
}

}  // namespace tamias
