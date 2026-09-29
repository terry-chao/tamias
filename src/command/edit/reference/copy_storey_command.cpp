#include "command/edit/reference/copy_storey_command.h"

#include "bim/host_geometry.h"
#include "bim/host_update.h"
#include "bim/wall_join.h"
#include "command/edit/transform/entity_transform.h"
#include "engine/base/log.h"
#include "entity/core/entity.h"
#include "entity/core/entity_storey.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tamias {
namespace {

Vec3 transform_origin(const Mat4& m) { return {m(0, 3), m(1, 3), m(2, 3)}; }

}  // namespace

CopyStoreyCommand::CopyStoreyCommand(Document& document, std::uint64_t source_storey_id,
                                     std::uint64_t target_storey_id)
    : CopyStoreyCommand(document, source_storey_id,
                        std::vector<std::uint64_t>{target_storey_id}) {}

CopyStoreyCommand::CopyStoreyCommand(Document& document, std::uint64_t source_storey_id,
                                     std::vector<std::uint64_t> target_storey_ids)
    : document_(&document),
      source_storey_id_(source_storey_id),
      target_storey_ids_(std::move(target_storey_ids)) {}

Result<void> CopyStoreyCommand::execute() {
  if (source_storey_id_ == 0) {
    return Err("copy_storey: a source floor is required");
  }
  if (target_storey_ids_.empty()) {
    return Err("copy_storey: at least one target floor is required");
  }
  if (document_->bim().find_storey(source_storey_id_) == nullptr) {
    return Err("copy_storey: floor not found");
  }

  // 命令口和对话框都允许一次给多层：去重后按传入顺序复制，整批共用一次撤销。
  std::vector<std::uint64_t> targets;
  std::unordered_set<std::uint64_t> seen;
  for (const std::uint64_t target : target_storey_ids_) {
    if (target == 0) {
      return Err("copy_storey: a target floor is required");
    }
    if (target == source_storey_id_) {
      return Err("copy_storey: the source and target floors must differ");
    }
    if (document_->bim().find_storey(target) == nullptr) {
      return Err("copy_storey: floor not found");
    }
    if (seen.insert(target).second) {
      targets.push_back(target);
    }
  }
  target_storey_ids_ = std::move(targets);

  if (!captured_) {
    capture_sources();
  }
  if (sources_.empty()) {
    return Err("copy_storey: the source floor has no components");
  }
  return create();
}

void CopyStoreyCommand::undo() { destroy(); }

void CopyStoreyCommand::redo() {
  // 用快照按原 id 重插：id 稳定，撤销/重做来回切不会让关系指向别处。
  for (const Made& made : made_) {
    document_->insert_entity(made.entity->clone(), made.mesh);
  }
  for (const Relation& relation : made_relations_) {
    document_->bim().insert(relation);
  }
  // 先把实体插回去（墙这时是实心的），再补上关系重算开洞。
  for (const std::uint64_t host_id : affected_hosts_) {
    if (auto r = remesh_host_openings(*document_, host_id); !r) {
      log_warn("copy_storey: redo remesh host failed: " + r.error());
    }
  }
  document_->recompute_scene();
  document_->mark_dirty();
}

void CopyStoreyCommand::capture_sources() {
  sources_.clear();
  for (const auto& [id, entity] : document_->entities()) {
    if (entity == nullptr || entity_storey_id(*entity) != source_storey_id_) {
      continue;
    }
    Source source;
    source.entity = entity->clone();
    if (const MeshAsset* mesh = document_->mesh(entity->mesh_asset_id)) {
      source.mesh = *mesh;
    }
    if (const Relation* host = document_->bim().host_of(id);
        host != nullptr && document_->entity(host->to) != nullptr) {
      source.is_opening = true;
      source.host_id = host->to;
    }
    if (entity->location != nullptr) {
      source.elevation_offset = entity->location->elevation_offset();
    }
    sources_.push_back(std::move(source));
  }
  captured_ = true;
}

Result<void> CopyStoreyCommand::create() {
  made_.clear();
  made_relations_.clear();
  created_ids_.clear();
  affected_hosts_.clear();

  for (const std::uint64_t target_storey_id : target_storey_ids_) {
    if (auto r = create_to_target(target_storey_id); !r) {
      destroy();
      return Err(r.error());
    }
  }

  // 复制的墙可能刚好顶到同一批副本墙的端点：交接（斜接面）要重算。
  for (const std::uint64_t id : created_ids_) {
    const Entity* entity = document_->entity(id);
    if (entity != nullptr && is_wall_host(*entity)) {
      if (auto r = remesh_wall_neighborhood(*document_, id); !r) {
        log_warn("copy_storey: remesh wall failed: " + r.error());
      }
    }
  }
  document_->recompute_scene();
  document_->mark_dirty();
  return {};
}

Result<void> CopyStoreyCommand::create_to_target(std::uint64_t target_storey_id) {
  // 楼层标高差 = 整层的竖直位移。相对本层的偏移不变 → 构件相对本层的上下关系不乱。
  const Vec3 lift{0.f, static_cast<float>(document_->bim().storey_elevation(target_storey_id) -
                                          document_->bim().storey_elevation(source_storey_id_)),
                  0.f};

  // 源 id → 本份副本 id。门窗靠它把宿主指向同一批里的新墙。
  std::unordered_map<std::uint64_t, std::uint64_t> id_map;

  // 第一遍：墙 / 板 / 柱 / 梁 / 幕墙 / 基础……先把宿主建出来。
  for (const Source& source : sources_) {
    if (source.is_opening) {
      continue;
    }
    std::unique_ptr<Entity> clone = source.entity->clone();
    clone->id = 0;
    if (clone->location != nullptr) {
      // 相对源楼层的标高偏移原样搬到目标层（世界标高 = 目标层标高 + 原偏移）。
      document_->assign_storey_with_offset(*clone, target_storey_id, source.elevation_offset);
    } else {
      // 没有 Location 的实体没有楼层归属，正常进不了复制范围；真进来了就按层高差平移。
      clone->local_transform = translate(lift) * clone->local_transform;
    }
    auto geometry = clone->createGeom();
    if (!geometry) {
      return Err("copy_storey: " + geometry.error());
    }
    Entity* added = document_->add_entity(std::move(clone), std::move(*geometry));
    if (added == nullptr) {
      return Err("copy_storey: add entity failed");
    }
    id_map[source.entity->id] = added->id;
    created_ids_.push_back(added->id);
    record_made(*added);
  }

  // 第二遍：门窗。宿主在同一批里被复制过就挂到新墙；够不着宿主的宁可跳过，
  // 也不把开口留在别的楼层上（那会变成一扇跨层的门）。
  for (const Source& source : sources_) {
    if (!source.is_opening) {
      continue;
    }
    const auto host_it = id_map.find(source.host_id);
    if (host_it == id_map.end()) {
      log_warn("copy_storey: skip opening " + std::to_string(source.entity->id) +
               " whose host is not on the source floor");
      continue;
    }
    const std::uint64_t host_id = host_it->second;
    const Mat4 world = translate(lift) * entity_world_transform(*source.entity);

    std::unique_ptr<Entity> clone = source.entity->clone();
    clone->id = 0;
    apply_entity_placement(*document_, *clone, world);
    auto geometry = clone->createGeom();
    if (!geometry) {
      return Err("copy_storey: " + geometry.error());
    }
    Entity* added = document_->add_entity(std::move(clone), std::move(*geometry));
    if (added == nullptr) {
      return Err("copy_storey: add opening failed");
    }
    created_ids_.push_back(added->id);
    if (auto r = bind_opening_to_host(*document_, added->id, host_id, transform_origin(world));
        !r) {
      return Err("copy_storey: " + r.error());
    }
    affected_hosts_.push_back(host_id);
    if (const Relation* relation = document_->bim().host_of(added->id)) {
      made_relations_.push_back(*relation);
    }
    record_made(*added);
  }

  return {};
}

void CopyStoreyCommand::record_made(const Entity& entity) {
  Made made;
  made.entity = entity.clone();
  if (const MeshAsset* mesh = document_->mesh(entity.mesh_asset_id)) {
    made.mesh = *mesh;
  }
  made_.push_back(std::move(made));
}

void CopyStoreyCommand::destroy() {
  // remove_entity 会连带删掉涉及的关系并重算邻墙；副本墙上的洞要单独补回来
  // （副本被删以后墙要恢复实心）。
  for (const Made& made : made_) {
    if (document_->entity(made.entity->id) != nullptr) {
      document_->remove_entity(made.entity->id);
    }
  }
  for (const std::uint64_t host_id : affected_hosts_) {
    if (document_->entity(host_id) != nullptr) {
      if (auto r = remesh_host_openings(*document_, host_id); !r) {
        log_warn("copy_storey: undo remesh host failed: " + r.error());
      }
    }
  }
  document_->recompute_scene();
  document_->mark_dirty();
}

}  // namespace tamias
