#include "command/edit/copy_entities_command.h"

#include "bim/host_geometry.h"
#include "bim/host_update.h"
#include "bim/wall_join.h"
#include "command/edit/entity_transform.h"
#include "engine/base/log.h"
#include "entity/core/entity.h"

#include <unordered_map>
#include <utility>

namespace tamias {
namespace {

Vec3 transform_origin(const Mat4& m) { return {m(0, 3), m(1, 3), m(2, 3)}; }

}  // namespace

CopyEntitiesCommand::CopyEntitiesCommand(Document& document,
                                         std::vector<std::uint64_t> source_ids,
                                         std::vector<Mat4> placements)
    : document_(&document), source_ids_(std::move(source_ids)), placements_(std::move(placements)) {}

Result<void> CopyEntitiesCommand::execute() {
  if (placements_.empty()) {
    return Err("CopyEntitiesCommand: nothing to copy");
  }
  if (!captured_) {
    capture_sources();
  }
  if (sources_.empty()) {
    return Err("CopyEntitiesCommand: no source entity found");
  }
  return create();
}

void CopyEntitiesCommand::undo() { destroy(); }

void CopyEntitiesCommand::redo() {
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
      log_warn("copy: redo remesh host failed: " + r.error());
    }
  }
  document_->recompute_scene();
  document_->mark_dirty();
}

void CopyEntitiesCommand::capture_sources() {
  sources_.clear();
  for (const std::uint64_t id : source_ids_) {
    Entity* entity = document_->entity(id);
    if (entity == nullptr) {
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
    sources_.push_back(std::move(source));
  }
  captured_ = true;
}

Result<void> CopyEntitiesCommand::create() {
  made_.clear();
  made_relations_.clear();
  created_ids_.clear();
  affected_hosts_.clear();

  for (const Mat4& placement : placements_) {
    // 源 id → 本份副本 id。门窗要靠它把宿主指向同一份里的新墙。
    std::unordered_map<std::uint64_t, std::uint64_t> id_map;

    // 第一遍：墙 / 板 / 柱 / 草图……先把宿主建出来。
    for (const Source& source : sources_) {
      if (source.is_opening) {
        continue;
      }
      std::unique_ptr<Entity> clone = source.entity->clone();
      clone->id = 0;
      apply_entity_placement(*document_, *clone, placement * entity_world_transform(*source.entity));
      auto geometry = clone->createGeom();
      if (!geometry) {
        destroy();
        return Err("copy: " + geometry.error());
      }
      Entity* added = document_->add_entity(std::move(clone), std::move(*geometry));
      if (added == nullptr) {
        destroy();
        return Err("copy: add entity failed");
      }
      id_map[source.entity->id] = added->id;
      created_ids_.push_back(added->id);
      Made made;
      made.entity = added->clone();
      if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
        made.mesh = *mesh;
      }
      made_.push_back(std::move(made));
    }

    // 第二遍：门窗。宿主在同一份里被复制过就连到新墙，否则留在原墙上。
    for (const Source& source : sources_) {
      if (!source.is_opening) {
        continue;
      }
      const auto host_it = id_map.find(source.host_id);
      const std::uint64_t host_id =
          host_it != id_map.end() ? host_it->second : source.host_id;
      const Mat4 world = placement * entity_world_transform(*source.entity);

      std::unique_ptr<Entity> clone = source.entity->clone();
      clone->id = 0;
      apply_entity_placement(*document_, *clone, world);
      auto geometry = clone->createGeom();
      if (!geometry) {
        destroy();
        return Err("copy: " + geometry.error());
      }
      Entity* added = document_->add_entity(std::move(clone), std::move(*geometry));
      if (added == nullptr) {
        destroy();
        return Err("copy: add opening failed");
      }
      created_ids_.push_back(added->id);
      if (auto r = bind_opening_to_host(*document_, added->id, host_id, transform_origin(world));
          !r) {
        destroy();
        return Err("copy: " + r.error());
      }
      affected_hosts_.push_back(host_id);
      if (const Relation* relation = document_->bim().host_of(added->id)) {
        made_relations_.push_back(*relation);
      }
      Made made;
      made.entity = added->clone();
      if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
        made.mesh = *mesh;
      }
      made_.push_back(std::move(made));
    }
  }

  // 复制的墙可能刚好顶到邻墙端点：交接（斜接面）要重算。bind 那一步已经建过一次
  // 宿主墙，这里补的是「副本墙 ↔ 周围墙」。
  for (const std::uint64_t id : created_ids_) {
    const Entity* entity = document_->entity(id);
    if (entity != nullptr && is_wall_host(*entity)) {
      if (auto r = remesh_wall_neighborhood(*document_, id); !r) {
        log_warn("copy: remesh wall failed: " + r.error());
      }
    }
  }
  document_->recompute_scene();
  document_->mark_dirty();
  return {};
}

void CopyEntitiesCommand::destroy() {
  // remove_entity 会连带删掉涉及的关系并重算邻墙；宿主墙上的洞要单独补回来
  // （尤其是「只复制门窗、宿主没被复制」那种）。
  for (const Made& made : made_) {
    if (document_->entity(made.entity->id) != nullptr) {
      document_->remove_entity(made.entity->id);
    }
  }
  for (const std::uint64_t host_id : affected_hosts_) {
    if (document_->entity(host_id) != nullptr) {
      if (auto r = remesh_host_openings(*document_, host_id); !r) {
        log_warn("copy: undo remesh host failed: " + r.error());
      }
    }
  }
  document_->recompute_scene();
  document_->mark_dirty();
}

}  // namespace tamias
