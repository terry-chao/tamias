#include "edit_entity_grip_command.h"

#include "bim/host_update.h"
#include "entity/entity_grip.h"
#include "engine/modeling/occt_geom_builder.h"

namespace tamias {

Result<void> rebuild_entity_mesh(Document& document, std::uint64_t entity_id) {
  Entity* entity = document.entity(entity_id);
  if (entity == nullptr) {
    return Err("rebuild_entity_mesh: entity not found");
  }
  auto mesh = entity->createGeom();
  if (!mesh) {
    return Err(mesh.error());
  }
  MeshAsset* asset = document.mesh(entity->mesh_asset_id);
  if (asset == nullptr) {
    return Err("rebuild_entity_mesh: mesh asset not found");
  }
  asset->cpu = std::move(*mesh);
  document.scene().set_transform(entity_id, entity->local_transform);
  document.recompute_scene();
  document.mark_dirty();
  if (auto r = notify_entity_changed(document, entity_id); !r) {
    return r;
  }
  return {};
}

EditEntityGripCommand::EditEntityGripCommand(Document& document, std::uint64_t entity_id,
                                             FeatureModel from_model, Mat4 from_transform,
                                             FeatureModel to_model, Mat4 to_transform)
    : document_(&document),
      entity_id_(entity_id),
      from_model_(std::move(from_model)),
      to_model_(std::move(to_model)),
      from_transform_(from_transform),
      to_transform_(to_transform) {}

Result<void> EditEntityGripCommand::execute() { return apply(true); }

void EditEntityGripCommand::undo() { (void)apply(false); }

void EditEntityGripCommand::redo() { (void)apply(true); }

Result<void> EditEntityGripCommand::apply(bool to_target) {
  Entity* entity = document_->entity(entity_id_);
  if (entity == nullptr) {
    return Err("EditEntityGripCommand: entity not found");
  }
  entity->model = to_target ? to_model_ : from_model_;
  entity->local_transform = to_target ? to_transform_ : from_transform_;
  sync_entity_grips(*entity);
  return rebuild_entity_mesh(*document_, entity_id_);
}

}  // namespace tamias
