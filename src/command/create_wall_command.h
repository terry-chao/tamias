#pragma once

#include "command/command.h"
#include "engine/math/math.h"
#include "entity/document.h"

namespace tamias {

// 创建一面墙（长方体）：墙厚 × 墙高 × 两点间长度，放置在两点之间的地面上。
class CreateWallCommand final : public Command {
 public:
  CreateWallCommand(Document& document, Vec3 start, Vec3 end, double thickness, double height);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }
  [[nodiscard]] std::uint64_t node_id() const { return node_.id; }

 private:
  Document* document_ = nullptr;
  Vec3 start_{};
  Vec3 end_{};
  double thickness_ = 0.2;
  double height_ = 3.0;
  MeshAsset mesh_{};
  SceneNode node_{};
};

}  // namespace tamias
