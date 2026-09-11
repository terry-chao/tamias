#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "engine/render/material.h"

#include <cstdint>

namespace tamias {

// 原地覆盖库材质（同 id）。引用该材质的全部对象立刻看到新参数。
class UpdateMaterialCommand final : public Command {
 public:
  UpdateMaterialCommand(Document& document, Material material);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  Material incoming_;
  Material old_;
  bool executed_ = false;
};

}  // namespace tamias
