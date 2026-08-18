#pragma once

#include "bim/relation.h"
#include "command/command.h"
#include "engine/document/document.h"

#include <optional>

namespace tamias {

// 单点放置的参数化构件。
enum class PrimitiveKind { Box, Cylinder, Column, Slab, Door, Window };

// 创建一个参数化基础几何体（交互式）：dispatch 后武装，点一个位置确定，然后 createGeom 造型。
class CreatePrimitiveCommand final : public Command {
 public:
  CreatePrimitiveCommand(Document& document, PrimitiveKind kind);

  [[nodiscard]] bool interactive() const override { return true; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] Result<bool> on_pick(Vec3 point, std::uint64_t picked_entity_id) override;

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  PrimitiveKind kind_ = PrimitiveKind::Box;
  Vec3 position_{};
  std::uint64_t host_id_ = 0;
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
  std::optional<Relation> relation_;
};

}  // namespace tamias
