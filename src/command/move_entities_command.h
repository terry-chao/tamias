#pragma once

#include "command/command.h"
#include "engine/document/document.h"

#include <cstdint>
#include <vector>

namespace tamias {

struct EntityTransform {
  std::uint64_t id = 0;
  Mat4 from = Mat4::identity();
  Mat4 to = Mat4::identity();
};

// 平移选中实体（可撤销）：改 local_transform，墙移动后通知开口跟随。
class MoveEntitiesCommand final : public Command {
 public:
  MoveEntitiesCommand(Document& document, std::vector<EntityTransform> items);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  void apply(bool to_target);

  Document* document_ = nullptr;
  std::vector<EntityTransform> items_;
};

}  // namespace tamias
