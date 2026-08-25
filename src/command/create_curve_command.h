#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "engine/modeling/curve_definition.h"

#include <memory>

namespace tamias {

class CreateCurveCommand final : public Command {
 public:
  CreateCurveCommand(Document& document, CurveDefinition definition);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  CurveDefinition definition_;
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
