#include "command/set_location_command.h"

#include "bim/host_update.h"

namespace tamias {

SetLocationCommand::SetLocationCommand(Document& document, std::uint64_t entity_id,
                                       std::uint64_t storey_id, double elevation_offset)
    : document_(&document),
      entity_id_(entity_id),
      to_storey_id_(storey_id),
      to_offset_(elevation_offset) {
  if (const Entity* entity = document.entity(entity_id); entity && entity->location) {
    from_storey_id_ = entity->location->storey_id();
    from_offset_ = entity->location->elevation_offset();
  }
}

Result<void> SetLocationCommand::execute() {
  return apply(to_storey_id_, to_offset_);
}

void SetLocationCommand::undo() {
  (void)apply(from_storey_id_, from_offset_);
}

void SetLocationCommand::redo() {
  (void)apply(to_storey_id_, to_offset_);
}

Result<void> SetLocationCommand::apply(std::uint64_t storey_id, double elevation_offset) {
  Entity* entity = document_->entity(entity_id_);
  if (entity == nullptr || !entity->location) {
    return Err("SetLocationCommand: entity has no location");
  }
  entity->location->set_storey_id(storey_id);
  entity->location->set_elevation_offset(elevation_offset);
  if (!document_->sync_entity_location(entity_id_)) {
    return Err("SetLocationCommand: failed to sync location");
  }
  return notify_entity_changed(*document_, entity_id_);
}

}  // namespace tamias
