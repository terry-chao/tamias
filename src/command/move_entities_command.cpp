#include "move_entities_command.h"

#include "bim/host_update.h"

namespace tamias {

MoveEntitiesCommand::MoveEntitiesCommand(Document& document, std::vector<EntityTransform> items)
    : document_(&document), items_(std::move(items)) {}

Result<void> MoveEntitiesCommand::execute() {
  apply(true);
  return {};
}

void MoveEntitiesCommand::undo() { apply(false); }

void MoveEntitiesCommand::redo() { apply(true); }

void MoveEntitiesCommand::apply(bool to_target) {
  for (const EntityTransform& item : items_) {
    const Mat4& local = to_target ? item.to : item.from;
    if (Entity* entity = document_->entity(item.id)) {
      const double storey_elevation =
          entity->location
              ? document_->bim().storey_elevation(entity->location->storey_id())
              : 0.0;
      entity->sync_location_from_transform(local, storey_elevation);
    }
    document_->scene().set_transform(item.id, local);
  }
  document_->recompute_scene();
  document_->mark_dirty();
  for (const EntityTransform& item : items_) {
    if (document_->entity(item.id) != nullptr) {
      (void)notify_entity_changed(*document_, item.id);
    }
  }
}

}  // namespace tamias
