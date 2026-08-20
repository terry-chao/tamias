#include "create_slab_command.h"

#include "entity/slab_entity.h"
#include "engine/modeling/curve_geom.h"

namespace tamias {
namespace {

bool nearly_same_xz(Vec3 a, Vec3 b) {
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return dx * dx + dz * dz < 1e-8f;
}

}  // namespace

CreateSlabCommand::CreateSlabCommand(Document& document, double thickness, double elevation)
    : document_(&document), thickness_(thickness), elevation_(elevation) {}

Result<bool> CreateSlabCommand::on_point(Vec3 point) {
  point.y = static_cast<float>(elevation_);
  if (!has_start_) {
    start_ = point;
    has_start_ = true;
    return false;
  }
  if (nearly_same_xz(start_, point)) {
    return false;
  }
  end_ = point;
  return true;
}

std::vector<Vec3> CreateSlabCommand::preview_polyline(Vec3 cursor) const {
  cursor.y = static_cast<float>(elevation_);
  if (!has_start_ || nearly_same_xz(start_, cursor)) {
    return {};
  }
  return sample_rect_xz(start_, cursor);
}

Result<void> CreateSlabCommand::execute() {
  SlabEntity slab(start_, end_, thickness_);
  auto geometry = slab.createGeom();
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::make_unique<SlabEntity>(std::move(slab)),
                                        std::move(*geometry));
  if (added == nullptr) {
    return Err("CreateSlabCommand: add_slab failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateSlabCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateSlabCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
