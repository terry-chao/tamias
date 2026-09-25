#include "command/edit/transform_entities_command.h"

#include "bim/host_geometry.h"
#include "bim/host_update.h"
#include "bim/wall_join.h"
#include "engine/base/log.h"

namespace tamias {

TransformEntitiesCommand::TransformEntitiesCommand(Document& document,
                                                   std::vector<EntityTransform> items)
    : document_(&document), items_(std::move(items)) {}

Result<void> TransformEntitiesCommand::execute() {
  if (items_.empty()) {
    return Err("TransformEntitiesCommand: nothing to transform");
  }
  return apply(true);
}

void TransformEntitiesCommand::undo() { (void)apply(false); }

void TransformEntitiesCommand::redo() { (void)apply(true); }

void TransformEntitiesCommand::capture_guests() {
  guests_.assign(items_.size(), GuestBinding{});
  for (std::size_t i = 0; i < items_.size(); ++i) {
    if (Relation* relation = document_->bim().host_of(items_[i].id); relation != nullptr) {
      guests_[i] = GuestBinding{relation->id, relation->placement, relation->valid};
    }
  }
  captured_ = true;
}

Result<void> TransformEntitiesCommand::apply(bool to_target) {
  if (!captured_) {
    capture_guests();
  }
  for (std::size_t i = 0; i < items_.size(); ++i) {
    const EntityTransform& item = items_[i];
    Entity* entity = document_->entity(item.id);
    if (entity == nullptr) {
      continue;
    }
    const Mat4& target = to_target ? item.to : item.from;
    const GuestBinding& binding = guests_[i];
    if (binding.relation_id != 0) {
      // 门窗：把新位置投影回宿主墙，重算「沿墙多远 / 离地多高」，摆放交给
      // notify_entity_changed → reshape_hosted 写回。
      Relation* relation = document_->bim().find(binding.relation_id);
      Entity* host = relation != nullptr ? document_->entity(relation->to) : nullptr;
      if (relation == nullptr || host == nullptr) {
        continue;
      }
      if (to_target) {
        const WallSize wall = wall_size(*host);
        const OpeningSize opening = opening_size(*entity);
        const Vec3 world{target(0, 3), target(1, 3), target(2, 3)};
        relation->placement = placement_from_world(*host, *entity, world);
        align_placement(relation->placement, wall, opening);
        relation->valid = placement_is_valid(relation->placement, wall, opening);
      } else {
        relation->placement = binding.placement;
        relation->valid = binding.valid;
      }
      continue;
    }
    set_entity_world_transform(*document_, *entity, target);
  }
  document_->recompute_scene();
  document_->mark_dirty();

  // 交接与开洞的重算放在摆放全部落定之后：一面墙动了，邻墙的斜接面、墙上的
  // 门窗都要跟着重算（顺序反了会按旧位置算一遍）。
  for (const EntityTransform& item : items_) {
    Entity* entity = document_->entity(item.id);
    if (entity == nullptr) {
      continue;
    }
    if (is_wall_host(*entity)) {
      if (auto r = remesh_wall_neighborhood(*document_, item.id); !r) {
        log_warn("transform: remesh wall failed: " + r.error());
      }
    }
    if (auto r = notify_entity_changed(*document_, item.id); !r) {
      log_warn("transform: notify failed: " + r.error());
    }
  }
  return {};
}

}  // namespace tamias
