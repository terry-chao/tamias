#include "set_feature_param_command.h"

#include "engine/modeling/feature.h"
#include "engine/modeling/occt_geom_builder.h"

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
#if defined(TAMIAS_HAS_OCCT)
  Entity* entity = document_->entity(entity_id_);
  if (entity == nullptr) {
    return Err("SetFeatureParamCommand: entity not found");
  }
  entity->model.set_param(feature_id_, param_name_, value);

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
  return {};
#else
  (void)value;
  return Err("SetFeatureParamCommand requires OCCT");
#endif
}

void SetFeatureParamCommand::undo() { (void)apply(old_value_); }
void SetFeatureParamCommand::redo() { (void)apply(new_value_); }

}  // namespace tamias
