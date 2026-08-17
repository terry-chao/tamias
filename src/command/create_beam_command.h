#pragma once

#include "command/command.h"
#include "engine/document/document.h"

namespace tamias {

// 创建参数化梁（交互式）：两个点确定跨度和方向，截面尺寸由参数给定。
class CreateBeamCommand final : public Command {
 public:
  CreateBeamCommand(Document& document, double width, double depth);

  [[nodiscard]] bool interactive() const override { return true; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] bool has_start() const override { return has_start_; }
  [[nodiscard]] Vec3 start() const override { return start_; }

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  double width_ = 0.3;
  double depth_ = 0.5;
  bool has_start_ = false;
  Vec3 start_{};
  Vec3 end_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
