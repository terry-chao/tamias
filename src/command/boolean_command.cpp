#include "boolean_command.h"

#include "engine/modeling/occt_geom_builder.h"

namespace tamias {

BooleanCommand::BooleanCommand(Document& document, std::uint64_t a_id, std::uint64_t b_id,
                               BooleanOp op)
    : document_(&document), a_id_(a_id), b_id_(b_id), op_(op) {}

Result<void> BooleanCommand::execute() {
  if (a_id_ == b_id_) {
    return Err("BooleanCommand: operands must be different entities");
  }
  const Entity* a = document_->entity(a_id_);
  const Entity* b = document_->entity(b_id_);
  if (a == nullptr || b == nullptr) {
    return Err("BooleanCommand: entity not found");
  }
  a_model_old_ = a->model;
  a_mesh_old_ = *document_->mesh(a->mesh_asset_id);
  b_entity_ = b->clone();
  b_mesh_ = *document_->mesh(b->mesh_asset_id);
  return apply(true);
}

Result<void> BooleanCommand::apply(bool combined) {
#if defined(TAMIAS_HAS_OCCT)
  Entity* a = document_->entity(a_id_);
  if (a == nullptr) {
    return Err("BooleanCommand: entity A not found");
  }
  if (combined) {
    a->model = a_model_old_;
    const std::uint64_t a_root = a_model_old_.output_feature()->id;
    auto remap = a->model.append(b_entity_->model);
    const std::uint64_t b_root = remap.at(b_entity_->model.output_feature()->id);
    a->model.add_feature(FeatureKind::Boolean, {a_root, b_root},
                         {{"operation", static_cast<double>(static_cast<std::uint8_t>(op_))}});

    auto mesh = geometry_builder().build(a->model, 0.05);
    if (!mesh) {
      a->model = a_model_old_;  // 回滚
      return Err(mesh.error());
    }
    MeshAsset* asset = document_->mesh(a->mesh_asset_id);
    if (asset == nullptr) {
      return Err("BooleanCommand: mesh asset not found");
    }
    asset->cpu = std::move(*mesh);
    document_->remove_entity(b_id_);
  } else {
    a->model = a_model_old_;
    MeshAsset* asset = document_->mesh(a->mesh_asset_id);
    if (asset == nullptr) {
      return Err("BooleanCommand: mesh asset not found");
    }
    asset->cpu = a_mesh_old_.cpu;
    document_->insert_entity(b_entity_->clone(), b_mesh_);
  }
  document_->recompute_scene();
  document_->mark_dirty();
  return {};
#else
  (void)combined;
  return Err("BooleanCommand requires OCCT");
#endif
}

void BooleanCommand::undo() { (void)apply(false); }
void BooleanCommand::redo() { (void)apply(true); }

}  // namespace tamias
