#include "command/create_curve_command.h"

#include "entity/bezier_entity.h"
#include "entity/bspline_entity.h"
#include "entity/line_entity.h"
#include "entity/nurbs_entity.h"
#include "entity/polyline_entity.h"

#include <utility>

namespace tamias {
namespace {

Result<std::unique_ptr<Entity>> make_curve(const CurveDefinition& definition) {
  if (definition.points.size() < 2) {
    return Err("create_curve needs at least two points");
  }

  switch (definition.kind) {
    case CurveKind::Unknown:
      return Err("unknown curve kind");
    case CurveKind::Line:
      if (definition.points.size() != 2) {
        return Err("line needs exactly two points");
      }
      return std::make_unique<LineEntity>(definition.points[0], definition.points[1]);
    case CurveKind::Polyline:
      return std::make_unique<PolylineEntity>(definition.points);
    case CurveKind::Bezier:
      return std::make_unique<BezierEntity>(definition.points);
    case CurveKind::BSpline:
      return std::make_unique<BSplineEntity>(definition.points, definition.degree);
    case CurveKind::Nurbs: {
      std::vector<float> weights;
      weights.reserve(definition.weights.size());
      for (const double weight : definition.weights) {
        weights.push_back(static_cast<float>(weight));
      }
      return std::make_unique<NurbsEntity>(definition.points, std::move(weights),
                                           definition.degree);
    }
  }
  return Err("unknown curve kind");
}

}  // namespace

CreateCurveCommand::CreateCurveCommand(Document& document, CurveDefinition definition)
    : document_(&document), definition_(std::move(definition)) {}

Result<void> CreateCurveCommand::execute() {
  auto built = make_curve(definition_);
  if (!built) {
    return Err(built.error());
  }
  auto geometry = (*built)->createGeom();
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::move(*built), std::move(*geometry));
  if (added == nullptr) {
    return Err("CreateCurveCommand: add entity failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateCurveCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateCurveCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
