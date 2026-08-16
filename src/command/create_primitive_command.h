#pragma once

#include "command/command.h"
#include "engine/math/math.h"
#include "engine/document/document.h"

namespace tamias {

// 基础几何体种类。
enum class PrimitiveKind { Box, Cylinder };

// 创建一个参数化基础几何体（box / cylinder）：command → BoxEntity::at / CylinderEntity::at → 实体 → Document。
class CreatePrimitiveCommand final : public Command {
 public:
  CreatePrimitiveCommand(Document& document, PrimitiveKind kind, Vec3 position);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  PrimitiveKind kind_ = PrimitiveKind::Box;
  Vec3 position_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
