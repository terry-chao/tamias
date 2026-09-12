#include "command/import_drawing_command.h"

#include "bim/host_update.h"
#include "entity/column_entity.h"
#include "entity/door_entity.h"
#include "entity/wall_entity.h"
#include "entity/window_entity.h"

#include <utility>

namespace tamias {

ImportDrawingCommand::ImportDrawingCommand(Document& document, DrawingImportPlan plan)
    : document_(&document), plan_(std::move(plan)) {}

Result<void> ImportDrawingCommand::execute() {
  if (!built_) {
    if (auto r = build(); !r) {
      drop_all();  // 建到一半失败：把已经建出来的收回去，别留半个模型
      created_.clear();
      opening_ids_.clear();
      relations_.clear();
      return r;
    }
    return {};
  }
  // 重做：实体（含 id）与关联一起放回去，不重新识别、不重算尺寸。
  for (const Created& created : created_) {
    document_->insert_entity(created.entity->clone(), created.mesh);
  }
  for (const Relation& relation : relations_) {
    document_->bim().insert(relation);
    if (relation.kind == RelationKind::HostedOn) {
      // 重做时墙是重新入树的，开洞要按关联再切一遍。
      (void)remesh_host_openings(*document_, relation.to);
    }
  }
  return {};
}

Result<void> ImportDrawingCommand::build() {
  created_.clear();
  opening_ids_.clear();
  relations_.clear();

  // ---- 墙 ----
  std::vector<std::uint64_t> wall_ids(plan_.walls.size(), 0);
  for (std::size_t i = 0; i < plan_.walls.size(); ++i) {
    const WallCandidate& candidate = plan_.walls[i];
    WallEntity wall(candidate.start, candidate.end, candidate.thickness, candidate.height);
    document_->assign_active_storey(wall);
    auto geometry = wall.createGeom();
    if (!geometry) {
      return Err("ImportDrawingCommand: wall geometry failed: " + geometry.error());
    }
    Entity* added = document_->add_entity(std::make_unique<WallEntity>(std::move(wall)),
                                          std::move(*geometry));
    if (added == nullptr) {
      return Err("ImportDrawingCommand: add wall failed");
    }
    wall_ids[i] = added->id;
    Created created;
    created.entity = added->clone();
    if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
      created.mesh = *mesh;
    }
    created_.push_back(std::move(created));
  }

  // ---- 柱 ----
  for (const ColumnCandidate& candidate : plan_.columns) {
    std::unique_ptr<ColumnEntity> column;
    if (candidate.circular) {
      column = std::make_unique<ColumnEntity>(
          ColumnEntity::circular(candidate.position, candidate.width, candidate.height));
    } else {
      column = std::make_unique<ColumnEntity>(candidate.position, candidate.width,
                                             candidate.depth, candidate.height);
    }
    document_->assign_active_storey(*column);
    auto geometry = column->createGeom();
    if (!geometry) {
      return Err("ImportDrawingCommand: column geometry failed: " + geometry.error());
    }
    Entity* added = document_->add_entity(std::move(column), std::move(*geometry));
    if (added == nullptr) {
      return Err("ImportDrawingCommand: add column failed");
    }
    Created created;
    created.entity = added->clone();
    if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
      created.mesh = *mesh;
    }
    created_.push_back(std::move(created));
  }

  // ---- 门窗：先建实体，再绑宿主墙（绑定会切穿墙、写 HostedOn 关系）----
  for (const OpeningCandidate& candidate : plan_.openings) {
    if (candidate.host_wall >= wall_ids.size() || wall_ids[candidate.host_wall] == 0) {
      continue;
    }
    std::unique_ptr<Entity> opening;
    if (candidate.door) {
      opening = std::make_unique<DoorEntity>(candidate.position, candidate.width,
                                             candidate.height, candidate.thickness, candidate.sill);
    } else {
      opening = std::make_unique<WindowEntity>(candidate.position, candidate.width,
                                               candidate.height, candidate.thickness,
                                               candidate.sill);
    }
    document_->assign_active_storey(*opening);
    auto geometry = opening->createGeom();
    if (!geometry) {
      continue;  // 单樘门窗造型失败不该让整次翻模失败
    }
    Entity* added = document_->add_entity(std::move(opening), std::move(*geometry));
    if (added == nullptr) {
      continue;
    }
    if (auto bound = bind_opening_to_host(*document_, added->id, wall_ids[candidate.host_wall],
                                          candidate.position);
        !bound) {
      document_->remove_entity(added->id);
      continue;
    }
    opening_ids_.push_back(added->id);
    if (const Relation* relation = document_->bim().host_of(added->id)) {
      relations_.push_back(*relation);
    }
    Created created;
    created.entity = added->clone();
    if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
      created.mesh = *mesh;
    }
    created_.push_back(std::move(created));
  }

  // 绑门窗会重切宿主墙的网格：统一重取一遍实体与网格，保证 redo 用的是最终几何。
  for (Created& created : created_) {
    if (const Entity* live = document_->entity(created.entity->id)) {
      created.entity = live->clone();
    }
    if (const MeshAsset* mesh = document_->mesh(created.entity->mesh_asset_id)) {
      created.mesh = *mesh;
    }
  }

  built_ = true;
  return {};
}

void ImportDrawingCommand::drop_all() {
  // 顺序无关：整批删除，门窗与墙都在里面，关系由 remove_entity 一并清掉。
  for (const Created& created : created_) {
    if (created.entity != nullptr) {
      document_->remove_entity(created.entity->id);
    }
  }
}

void ImportDrawingCommand::undo() {
  if (!built_) {
    return;
  }
  drop_all();
}

void ImportDrawingCommand::redo() {
  if (auto r = execute(); !r) {
    // redo 失败不回滚：状态与报错都留给上层，和别的命令一致。
    return;
  }
}

}  // namespace tamias
