#include "add_feature_command.h"

#include "engine/modeling/occt_geom_builder.h"

namespace tamias {

AddFeatureCommand::AddFeatureCommand(Document& document, std::uint64_t entity_id, FeatureKind kind,
                                     std::unordered_map<std::string, double> params)
    : document_(&document),
      entity_id_(entity_id),
      kind_(kind),
      params_(std::move(params)) {}

Result<void> AddFeatureCommand::apply(bool add) {
#if defined(TAMIAS_HAS_OCCT)
  Entity* entity = document_->entity(entity_id_);
  if (entity == nullptr) {
    return Err("AddFeatureCommand: entity not found");
  }
  if (add) {
    if (entity->model.features().empty()) {
      return Err("AddFeatureCommand: entity has no geometry");
    }
    const std::uint64_t input = entity->model.output_feature()->id;
    Feature& added = entity->model.add_feature(kind_, {input}, params_);
    feature_id_ = added.id;
  } else {
    entity->model.remove_feature(feature_id_);
  }

  auto mesh = geometry_builder().build(entity->model, 0.05);
  if (!mesh) {
    return Err(mesh.error());
  }
  MeshAsset* asset = document_->mesh(entity->mesh_asset_id);
  if (asset == nullptr) {
    return Err("AddFeatureCommand: mesh asset not found");
  }
  asset->cpu = std::move(*mesh);
  document_->recompute_scene();
  document_->mark_dirty();
  return {};
#else
  (void)add;
  return Err("AddFeatureCommand requires OCCT");
#endif
}

Result<void> AddFeatureCommand::execute() { return apply(true); }
void AddFeatureCommand::undo() { (void)apply(false); }
void AddFeatureCommand::redo() { (void)apply(true); }

}  // namespace tamias
