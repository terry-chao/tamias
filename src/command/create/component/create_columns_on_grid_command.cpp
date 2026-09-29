#include "command/create/component/create_columns_on_grid_command.h"

#include <cmath>
#include <utility>

namespace tamias {
namespace {

// 同一个平面点的判定容差（米）：1 mm 以内就算「这里已经有一根柱了」。
constexpr double kSameColumnTolerance = 1e-3;

// 柱子已经站在这个点了？按**柱底的三维位置**比（local_transform 的 x / y / z）：
// 轴交柱都在轴网平面上，同一个平面点再放一次就是重叠；而不同标高上的柱（比如上层
// 也有一根对着的柱）不算重复。
bool column_exists_at(const Document& document, Vec3 base) {
  for (const auto& [id, entity] : document.entities()) {
    (void)id;
    if (entity == nullptr || entity->kind() != EntityKind::Column) {
      continue;
    }
    const double dx = std::fabs(static_cast<double>(entity->local_transform(0, 3)) - base.x);
    const double dy = std::fabs(static_cast<double>(entity->local_transform(1, 3)) - base.y);
    const double dz = std::fabs(static_cast<double>(entity->local_transform(2, 3)) - base.z);
    if (dx <= kSameColumnTolerance && dy <= kSameColumnTolerance &&
        dz <= kSameColumnTolerance) {
      return true;
    }
  }
  return false;
}

// 站在这个标高上的是哪一层？找不到就一直未归属（0）。轴交柱都落在轴网平面（y = 0），
// 所以通常落到地面层。
std::uint64_t storey_id_at_elevation(const Document& document, double elevation) {
  for (const Storey& storey : document.bim().storeys()) {
    if (std::fabs(storey.elevation - elevation) <= kSameColumnTolerance) {
      return storey.id;
    }
  }
  return 0;
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

  // 柱立在**轴网平面**上（轴网是地面 / 平面参考，数据恒在 y = 0，见 bim/grid.h）：
  // 不再抬到当前楼层标高——轴网在 y = 0、柱却按楼层标高立的话，柱会飘在轴网上方。
  // 楼层归属跟着柱底走：标高等于轴网平面的那一层（通常是地面层）；没有就一直未归属。
  const double elevation = kGridPlaneY;
  const std::uint64_t storey_id = storey_id_at_elevation(*document_, elevation);
  const std::vector<Vec3> points =
      grid_intersections(document_->bim().grid().axes(), axis_ids_);
  if (points.empty()) {
    built_ = true;  // 没交点 = 什么都不做，命令照样算成功（状态栏由调用方回显）
    return {};
  }

  for (const Vec3& point : points) {
    const Vec3 position{point.x, static_cast<float>(elevation), point.z};
    if (column_exists_at(*document_, position)) {
      ++skipped_;
      continue;
    }
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
