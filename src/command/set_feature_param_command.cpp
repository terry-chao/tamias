#include "set_feature_param_command.h"

#include "bim/line_location.h"
#include "bim/host_update.h"
#include "entity/entity_grip.h"
#include "engine/modeling/feature.h"
#include "engine/modeling/occt_geom_builder.h"

#include <algorithm>
#include <cmath>

namespace tamias {

SetFeatureParamCommand::SetFeatureParamCommand(Document& document, std::uint64_t entity_id,
                                               std::uint64_t feature_id, std::string param_name,
                                               double new_value)
    : document_(&document),
      entity_id_(entity_id),
      feature_id_(feature_id),
      param_name_(std::move(param_name)),
      new_value_(new_value) {}

Result<void> SetFeatureParamCommand::execute() {
  Entity* entity = document_->entity(entity_id_);
  if (entity == nullptr) {
    return Err("SetFeatureParamCommand: entity not found");
  }
  mesh_asset_id_ = entity->mesh_asset_id;
  const Feature* f = entity->model.find(feature_id_);
  if (f == nullptr) {
    return Err("SetFeatureParamCommand: feature not found");
  }
  const auto it = f->params.find(param_name_);
  old_value_ = (it != f->params.end()) ? it->second : 0.0;
  return apply(new_value_);
}

Result<void> SetFeatureParamCommand::apply(double value) {
  Entity* entity = document_->entity(entity_id_);
  if (entity == nullptr) {
    return Err("SetFeatureParamCommand: entity not found");
  }
  entity->model.set_param(feature_id_, param_name_, value);
  const Feature* changed = entity->model.find(feature_id_);
  if (entity->kind() == EntityKind::Wall && entity->location &&
      entity->location->kind() == LocationKind::Line && changed != nullptr &&
      changed->kind == FeatureKind::RectProfile && param_name_ == "height") {
    auto* line = static_cast<LineLocation*>(entity->location.get());
    const Vec3 start = line->start();
    const Vec3 end = line->end();
    const Vec3 mid = (start + end) * 0.5f;
    const Vec3 delta = end - start;
    const float old_length = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const Vec3 direction =
        old_length > 1e-6f ? delta * (1.f / old_length) : Vec3{0.f, 0.f, 1.f};
    const float half = static_cast<float>(std::max(value, 1e-3) * 0.5);
    line->set_start(mid - direction * half);
    line->set_end(mid + direction * half);
    entity->sync_from_location(
        document_->bim().storey_elevation(entity->location->storey_id()));
    document_->scene().set_transform(entity_id_, entity->local_transform);
  }
  sync_entity_grips(*entity);

  auto mesh = geometry_builder().build(entity->model, 0.05);
  if (!mesh) {
    return Err(mesh.error());
  }
  MeshAsset* asset = document_->mesh(entity->mesh_asset_id);
  if (asset == nullptr) {
    return Err("SetFeatureParamCommand: mesh asset not found");
  }
  asset->cpu = std::move(*mesh);
  document_->recompute_scene();
  document_->mark_dirty();
  if (auto r = notify_entity_changed(*document_, entity_id_); !r) {
    return r;
  }
  return {};
}

void SetFeatureParamCommand::undo() { (void)apply(old_value_); }
void SetFeatureParamCommand::redo() { (void)apply(new_value_); }

}  // namespace tamias
