#pragma once

#include "command/command.h"
#include "engine/graphics/mesh.h"
#include "engine/math/math.h"
#include "entity/document.h"

#include <string>

namespace tamias {

// 创建一个基础几何体（box / cylinder 等），放置在指定位置。
class CreatePrimitiveCommand final : public Command {
 public:
  CreatePrimitiveCommand(Document& document, MeshCpu mesh, std::string name, Vec3 position);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }
  [[nodiscard]] std::uint64_t node_id() const { return node_.id; }

 private:
  Document* document_ = nullptr;
  MeshCpu cpu_mesh_{};
  std::string name_;
  Vec3 position_{};
  MeshAsset mesh_{};
  SceneNode node_{};
};

}  // namespace tamias
