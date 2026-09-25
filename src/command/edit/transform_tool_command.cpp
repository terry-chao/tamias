#include "command/edit/transform_tool_command.h"

#include "command/edit/copy_entities_command.h"
#include "command/edit/entity_transform.h"
#include "command/edit/mirror_entities_command.h"
#include "command/edit/transform_entities_command.h"
#include "entity/core/entity.h"
#include "entity/core/entity_grip.h"

#include <cmath>

namespace tamias {
namespace {

// XZ 平面内的方位角，和 rotate_y 同一约定（+Z 转到 (sin a, 0, cos a)）。
double azimuth(Vec3 v) { return std::atan2(static_cast<double>(v.x), static_cast<double>(v.z)); }

bool nearly_same(Vec3 a, Vec3 b) { return length(a - b) < 1e-5f; }

}  // namespace

TransformToolCommand::TransformToolCommand(Document& document, Mode mode,
                                           std::vector<std::uint64_t> entity_ids)
    : document_(&document), mode_(mode), entity_ids_(std::move(entity_ids)) {
  for (const std::uint64_t id : entity_ids_) {
    if (const Entity* entity = document.entity(id); entity != nullptr) {
      work_plane_y_ = entity->local_transform(1, 3);
      break;
    }
  }
}

int TransformToolCommand::required_points() const {
  return mode_ == Mode::Rotate ? 3 : 2;
}

Result<bool> TransformToolCommand::on_point(Vec3 point) {
  if (!points_.empty() && nearly_same(points_.back(), point)) {
    return false;
  }
  points_.push_back(point);
  return static_cast<int>(points_.size()) >= required_points();
}

Result<Mat4> TransformToolCommand::placement_for(Vec3 cursor) const {
  if (points_.empty()) {
    return Err("transform: no base point yet");
  }
  switch (mode_) {
    case Mode::Move:
    case Mode::Copy:
      return translation_transform(cursor - points_.front());
    case Mode::Rotate: {
      if (points_.size() < 2) {
        return Err("transform: no reference direction yet");
      }
      const double delta =
          azimuth(cursor - points_.front()) - azimuth(points_[1] - points_.front());
      return yaw_rotation_about(points_.front(), delta);
    }
    case Mode::Mirror:
      return Mat4::identity();  // 镜像不是刚体变换，预览单独走 mirror_point
  }
  return Mat4::identity();
}

std::vector<Vec3> TransformToolCommand::preview_polyline(Vec3 cursor) const {
  if (points_.empty()) {
    return {};
  }
  switch (mode_) {
    case Mode::Rotate:
      if (points_.size() >= 2) {
        // 基点→参照 与 基点→光标：两段一起画（重复点会被折线跳过，不产生退化线段）。
        return {points_[0], points_[1], points_[0], cursor};
      }
      return {points_[0], cursor};
    case Mode::Move:
    case Mode::Copy:
    case Mode::Mirror:
      return {points_[0], cursor};
  }
  return {};
}

std::vector<Vec3> TransformToolCommand::preview_points(Vec3 cursor) const {
  std::vector<Vec3> out;
  if (points_.empty()) {
    return out;
  }
  if (mode_ == Mode::Mirror) {
    const Vec3 direction = cursor - points_.front();
    if (length({direction.x, 0.f, direction.z}) < 1e-5f) {
      return out;
    }
    for (const std::uint64_t id : entity_ids_) {
      const Entity* entity = document_->entity(id);
      if (entity == nullptr) {
        continue;
      }
      for (const EntityGrip& grip : collect_entity_grips(*entity)) {
        out.push_back(mirror_point(grip.world, points_.front(), direction));
      }
    }
    return out;
  }
  auto placement = placement_for(cursor);
  if (!placement) {
    return out;
  }
  for (const std::uint64_t id : entity_ids_) {
    const Entity* entity = document_->entity(id);
    if (entity == nullptr) {
      continue;
    }
    for (const EntityGrip& grip : collect_entity_grips(*entity)) {
      out.push_back(*placement * grip.world);
    }
  }
  return out;
}

CommandArgs TransformToolCommand::echo_args() const { return {{"points", points_}}; }

Result<std::unique_ptr<Command>> TransformToolCommand::build() const {
  if (static_cast<int>(points_.size()) < required_points()) {
    return Err("transform: not enough points");
  }
  if (mode_ == Mode::Mirror) {
    return std::unique_ptr<Command>{std::make_unique<MirrorEntitiesCommand>(
        *document_, entity_ids_, points_[0], points_[1])};
  }
  if (mode_ == Mode::Copy) {
    std::vector<Mat4> placements{translation_transform(points_[1] - points_[0])};
    return std::unique_ptr<Command>{std::make_unique<CopyEntitiesCommand>(
        *document_, entity_ids_, std::move(placements))};
  }
  // Move / Rotate：每个实体算一份 from → to。
  Mat4 placement = Mat4::identity();
  if (mode_ == Mode::Move) {
    placement = translation_transform(points_[1] - points_[0]);
  } else {
    const double delta =
        azimuth(points_[2] - points_[0]) - azimuth(points_[1] - points_[0]);
    placement = yaw_rotation_about(points_[0], delta);
  }
  std::vector<EntityTransform> items;
  items.reserve(entity_ids_.size());
  for (const std::uint64_t id : entity_ids_) {
    const Entity* entity = document_->entity(id);
    if (entity == nullptr) {
      continue;
    }
    EntityTransform item;
    item.id = id;
    item.from = entity_world_transform(*entity);
    item.to = placement * item.from;
    items.push_back(item);
  }
  if (items.empty()) {
    return Err("transform: selection is gone");
  }
  return std::unique_ptr<Command>{
      std::make_unique<TransformEntitiesCommand>(*document_, std::move(items))};
}

Result<void> TransformToolCommand::execute() {
  auto built = build();
  if (!built) {
    return Err(built.error());
  }
  inner_ = std::move(*built);
  return inner_->execute();
}

void TransformToolCommand::undo() {
  if (inner_ != nullptr) {
    inner_->undo();
  }
}

void TransformToolCommand::redo() {
  if (inner_ != nullptr) {
    inner_->redo();
  }
}

}  // namespace tamias
