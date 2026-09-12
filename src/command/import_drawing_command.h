#pragma once

#include "bim/drawing_import.h"
#include "bim/relation.h"
#include "command/command.h"
#include "engine/document/document.h"
#include "engine/document/mesh_asset.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {

// 把翻模识别的候选计划一次落进文档：墙 → 柱 → 门窗（门窗再绑到刚建的墙上）。
// 一整次翻模 = 一条命令 = 一步撤销（undo 栈本来就是整文档快照）。
class ImportDrawingCommand final : public Command {
 public:
  ImportDrawingCommand(Document& document, DrawingImportPlan plan);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] const DrawingImportPlan& plan() const { return plan_; }
  [[nodiscard]] std::size_t created_count() const { return created_.size(); }
  // 实际落到文档里的数量（绑定失败的门窗不计入）。
  [[nodiscard]] std::size_t created_openings() const { return opening_ids_.size(); }

 private:
  struct Created {
    std::unique_ptr<Entity> entity;
    MeshAsset mesh{};
  };

  Result<void> build();
  void drop_all();

  Document* document_ = nullptr;
  DrawingImportPlan plan_;
  std::vector<Created> created_;
  std::vector<std::uint64_t> opening_ids_;
  std::vector<Relation> relations_;
  bool built_ = false;
};

}  // namespace tamias
