#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "engine/render/material.h"

#include <cstdint>

namespace tamias {

// 给实体分配/新建材质（可撤销）。纯视觉：不重建网格，只 mark_dirty。
// material.id != 0 且已入库 → 引用现有库材质；id == 0 → 新建入库并引用。
class SetMaterialCommand final : public Command {
 public:
  SetMaterialCommand(Document& document, std::uint64_t entity_id, Material material);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  std::uint64_t entity_id_ = 0;
  std::uint64_t old_material_id_ = 0;
  std::uint64_t new_material_id_ = 0;
  Material material_;
};

}  // namespace tamias
