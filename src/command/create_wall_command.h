#pragma once

#include "command/command.h"
#include "engine/math/math.h"
#include "engine/document/document.h"

namespace tamias {

// 创建一面参数化墙：command → drag（Wall::drag）→ Wall 实体 → Document（内部建网格 + 场景节点）。
class CreateWallCommand final : public Command {
 public:
  CreateWallCommand(Document& document, Vec3 start, Vec3 end, double thickness, double height);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  Vec3 start_{};
  Vec3 end_{};
  double thickness_ = 0.2;
  double height_ = 3.0;
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
