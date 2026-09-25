#include "command/create/create_columns_on_grid_command.h"

#include "entity/core/entity_storey.h"

#include <cmath>
#include <utility>

namespace tamias {
namespace {

// 同一个平面点的判定容差（米）：1 mm 以内就算「这里已经有一根柱了」。
constexpr double kSameColumnTolerance = 1e-3;

// 这一层、这个平面点上是不是已经站着柱了？看柱的世界平面位置（local_transform 的
// x / z），不看标高——同一层里的柱标高都一样，分不分楼层由调用方先比过。
bool column_exists_at(const Document& document, Vec3 plan, std::uint64_t storey_id) {
  for (const auto& [id, entity] : document.entities()) {
    (void)id;
    if (entity == nullptr || entity->kind() != EntityKind::Column) {
      continue;
    }
    if (entity_storey_id(*entity) != storey_id) {
      continue;
    }
    const double dx = std::fabs(static_cast<double>(entity->local_transform(0, 3)) - plan.x);
    const double dz = std::fabs(static_cast<double>(entity->local_transform(2, 3)) - plan.z);
    if (dx <= kSameColumnTolerance && dz <= kSameColumnTolerance) {
      return true;
    }
  }
  return false;
}

}  // namespace

CreateColumnsOnGridCommand::CreateColumnsOnGridCommand(Document& document, ColumnShape shape,
                                                       double size_a, double size_b,
                                                       double height,
                                                       std::vector<std::uint64_t> axis_ids)
    : document_(&document),
      shape_(shape),
      size_a_(size_a),
      size_b_(size_b),
      height_(height),
      axis_ids_(std::move(axis_ids)) {}

Result<void> CreateColumnsOnGridCommand::execute() {
  if (!built_) {
    if (auto r = build(); !r) {
      drop_all();  // 建到一半失败：把已经建出来的收回去，别留半个模型
      created_.clear();
      return r;
    }
    return {};
  }
  // 重做：实体（含 id）与网格原样放回去，不重新求交点、不重算尺寸。
  for (const Created& created : created_) {
    document_->insert_entity(created.entity->clone(), created.mesh);
  }
  return {};
}

Result<void> CreateColumnsOnGridCommand::build() {
  created_.clear();
  skipped_ = 0;

  // 楼层归属取「执行这一刻」的当前楼层；柱底标高就是这一层的标高。
  const std::uint64_t storey_id = document_->bim().active_storey_id();
  const double elevation = document_->bim().storey_elevation(storey_id);
  const std::vector<Vec3> points =
      grid_intersections(document_->bim().grid().axes(), axis_ids_);
  if (points.empty()) {
    built_ = true;  // 没交点 = 什么都不做，命令照样算成功（状态栏由调用方回显）
    return {};
  }

  for (const Vec3& point : points) {
    if (column_exists_at(*document_, point, storey_id)) {
      ++skipped_;
      continue;
    }
    const Vec3 position{point.x, static_cast<float>(elevation), point.z};
    std::unique_ptr<ColumnEntity> column;
    if (shape_ == ColumnShape::Circular) {
      column = std::make_unique<ColumnEntity>(
          ColumnEntity::circular(position, size_a_, height_));
    } else {
      column = std::make_unique<ColumnEntity>(position, size_a_, size_b_, height_);
    }
    document_->assign_storey(*column, storey_id);
    auto geometry = column->createGeom();
    if (!geometry) {
      return Err(geometry.error());
    }
    Entity* added = document_->add_entity(std::move(column), std::move(*geometry));
    if (added == nullptr) {
      return Err("CreateColumnsOnGridCommand: add column failed");
    }
    Created created;
    created.entity = added->clone();
    if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
      created.mesh = *mesh;
    }
    created_.push_back(std::move(created));
  }
  built_ = true;
  return {};
}

void CreateColumnsOnGridCommand::drop_all() {
  for (const Created& created : created_) {
    if (created.entity != nullptr) {
      document_->remove_entity(created.entity->id);
    }
  }
}

void CreateColumnsOnGridCommand::undo() {
  if (!built_) {
    return;
  }
  drop_all();
}

void CreateColumnsOnGridCommand::redo() {
  if (auto r = execute(); !r) {
    return;  // redo 失败不回滚：状态与报错都留给上层，和别的命令一致
  }
}

}  // namespace tamias
