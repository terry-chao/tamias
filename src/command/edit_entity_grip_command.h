#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "engine/modeling/feature.h"

#include <cstdint>

namespace tamias {

// Rebuild mesh + scene after an entity's model/transform changed.
[[nodiscard]] Result<void> rebuild_entity_mesh(Document& document, std::uint64_t entity_id);

// Drag a grip: snapshot from/to feature tree + placement, then regenerate (undoable).
class EditEntityGripCommand final : public Command {
 public:
  EditEntityGripCommand(Document& document, std::uint64_t entity_id, FeatureModel from_model,
                        Mat4 from_transform, FeatureModel to_model, Mat4 to_transform);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Result<void> apply(bool to_target);

  Document* document_ = nullptr;
  std::uint64_t entity_id_ = 0;
  FeatureModel from_model_;
  FeatureModel to_model_;
  Mat4 from_transform_ = Mat4::identity();
  Mat4 to_transform_ = Mat4::identity();
};

}  // namespace tamias
