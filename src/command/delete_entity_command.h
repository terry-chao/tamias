#pragma once

#include "bim/relation.h"
#include "command/command.h"
#include "engine/document/document.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {

// 删除实体（连带网格与相关 BIM 关联），可撤销。
class DeleteEntityCommand final : public Command {
 public:
  DeleteEntityCommand(Document& document, std::uint64_t entity_id);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  std::uint64_t entity_id_ = 0;
  std::unique_ptr<Entity> entity_;
  MeshAsset mesh_{};
  std::vector<Relation> relations_;
};

}  // namespace tamias
