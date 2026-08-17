#include "create_beam_command.h"

#include "entity/beam_entity.h"

namespace tamias {

CreateBeamCommand::CreateBeamCommand(Document& document, double width, double depth)
    : document_(&document), width_(width), depth_(depth) {}

Result<bool> CreateBeamCommand::on_point(Vec3 point) {
  if (!has_start_) {
    start_ = point;
    has_start_ = true;
    return false;
  }
  end_ = point;
  return true;
}

Result<void> CreateBeamCommand::execute() {
  BeamEntity beam(start_, end_, width_, depth_);
  auto geometry = beam.createGeom();
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::make_unique<BeamEntity>(std::move(beam)),
                                        std::move(*geometry));
  if (added == nullptr) {
    return Err("CreateBeamCommand: add_beam failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateBeamCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateBeamCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
