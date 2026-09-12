#include "edit_entity_grip_command.h"

#include "bim/host_update.h"
#include "bim/wall_join.h"
#include "entity/entity_grip.h"
#include "engine/modeling/occt_geom_builder.h"

namespace tamias {

Result<void> rebuild_entity_mesh(Document& document, std::uint64_t entity_id) {
  Entity* entity = document.entity(entity_id);
  if (entity == nullptr) {
    return Err("rebuild_entity_mesh: entity not found");
  }
  // 墙要带上墙-墙倒角（以及宿主开口切减），其余实体就是自己的特征树。
  FeatureModel model = wall_render_model(*entity, document);
  auto mesh = geometry_builder().build(model, 0.05);
  if (!mesh) {
    return Err(mesh.error());
  }
  if (!document.replace_entity_mesh(entity_id, std::move(*mesh))) {
    return Err("rebuild_entity_mesh: mesh asset not found");
  }
  document.scene().set_transform(entity_id, entity->local_transform);
  document.recompute_scene();
  document.mark_dirty();
  // 交接变了（墙挪了 / 改了厚薄长高）：邻墙的斜接面也要重建（本墙上面刚建过）。
  if (auto r = remesh_wall_neighbors(document, entity_id); !r) {
    return r;
  }
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
  const Mat4 transform = to_target ? to_transform_ : from_transform_;
  const double storey_elevation =
      entity->location
          ? document_->bim().storey_elevation(entity->location->storey_id())
          : 0.0;
  entity->sync_location_from_transform(transform, storey_elevation);
  sync_entity_grips(*entity);
  return rebuild_entity_mesh(*document_, entity_id_);
}

}  // namespace tamias
