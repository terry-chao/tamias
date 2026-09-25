#include "command/edit/mirror_entities_command.h"

#include "bim/host_geometry.h"
#include "command/edit/edit_entity_grip_command.h"
#include "command/edit/entity_transform.h"
#include "engine/base/log.h"
#include "entity/core/entity.h"

namespace tamias {

MirrorEntitiesCommand::MirrorEntitiesCommand(Document& document,
                                             std::vector<std::uint64_t> entity_ids,
                                             Vec3 axis_a, Vec3 axis_b)
    : document_(&document),
      entity_ids_(std::move(entity_ids)),
      axis_a_(axis_a),
      axis_b_(axis_b) {}

Result<void> MirrorEntitiesCommand::execute() {
  if (entity_ids_.empty()) {
    return Err("MirrorEntitiesCommand: nothing to mirror");
  }
  const Vec3 direction = axis_b_ - axis_a_;
  if (length({direction.x, 0.f, direction.z}) < 1e-5f) {
    return Err("MirrorEntitiesCommand: mirror axis is too short");
  }
  if (!captured_) {
    capture();
  }
  return apply(true);
}

void MirrorEntitiesCommand::undo() { (void)apply(false); }

void MirrorEntitiesCommand::redo() { (void)apply(true); }

void MirrorEntitiesCommand::capture() {
  items_.clear();
  for (const std::uint64_t id : entity_ids_) {
    Entity* entity = document_->entity(id);
    if (entity == nullptr) {
      continue;
    }
    Item item;
    item.id = id;
    item.before = entity->clone();
    if (Relation* relation = document_->bim().host_of(id); relation != nullptr) {
      item.relation_id = relation->id;
      item.placement = relation->placement;
      item.valid = relation->valid;
    }
    items_.push_back(std::move(item));
  }
  captured_ = true;
}

Result<void> MirrorEntitiesCommand::apply(bool to_target) {
  // 每次都从快照出发：redo 幂等，不会在已镜像的结果上再镜像一次。
  for (Item& item : items_) {
    Entity* entity = document_->entity(item.id);
    if (entity == nullptr || item.before == nullptr) {
      continue;
    }
    if (item.relation_id != 0) {
      Relation* relation = document_->bim().find(item.relation_id);
      Entity* host = relation != nullptr ? document_->entity(relation->to) : nullptr;
      if (relation == nullptr || host == nullptr) {
        continue;
      }
      if (to_target) {
        const Mat4& before = item.before->local_transform;
        const Vec3 world{before(0, 3), before(1, 3), before(2, 3)};
        const WallSize wall = wall_size(*host);
        const OpeningSize opening = opening_size(*entity);
        relation->placement = placement_from_world(*host, *entity,
                                                   mirror_point(world, axis_a_, axis_b_ - axis_a_));
        align_placement(relation->placement, wall, opening);
        relation->valid = placement_is_valid(relation->placement, wall, opening);
      } else {
        relation->placement = item.placement;
        relation->valid = item.valid;
      }
    } else {
      entity->model = item.before->model;
      entity->local_transform = item.before->local_transform;
      entity->location = item.before->location ? item.before->location->clone() : nullptr;
      entity->grips = item.before->grips;
      if (to_target) {
        if (auto r = mirror_entity(*document_, *entity, axis_a_, axis_b_ - axis_a_); !r) {
          return r;
        }
      }
    }
    if (auto r = rebuild_entity_mesh(*document_, item.id); !r) {
      log_warn("mirror: rebuild failed: " + r.error());
    }
  }
  document_->recompute_scene();
  document_->mark_dirty();
  return {};
}

}  // namespace tamias
