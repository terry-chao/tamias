#pragma once

#include "command/command.h"
#include "engine/document/document.h"

namespace tamias {

// 创建参数化结构墙/剪力墙（交互式）：dispatch 后武装，拖拽两点确定墙。
class CreateStructuralWallCommand final : public Command {
 public:
  CreateStructuralWallCommand(Document& document, double thickness, double height);
  CreateStructuralWallCommand(Document& document, double thickness, double height,
                               Vec3 start, Vec3 end);

  [[nodiscard]] bool interactive() const override { return !scripted_; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] bool has_start() const override { return has_start_; }
  [[nodiscard]] Vec3 start() const override { return start_; }
  [[nodiscard]] float work_plane_y() const override {
    return static_cast<float>(elevation_);
  }

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  double thickness_ = 0.3;
  double height_ = 3.0;
  double elevation_ = 0.0;
  bool has_start_ = false;
  bool scripted_ = false;
  Vec3 start_{};
  Vec3 end_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
