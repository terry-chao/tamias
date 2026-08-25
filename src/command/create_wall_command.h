#pragma once

#include "command/command.h"
#include "engine/document/document.h"

namespace tamias {

// 创建一面参数化墙（交互式）：dispatch 后武装，拖拽两个点确定墙，然后 createGeom 造型。
class CreateWallCommand final : public Command {
 public:
  CreateWallCommand(Document& document, double thickness, double height);

  [[nodiscard]] bool interactive() const override { return true; }
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
  double thickness_ = 0.2;
  double height_ = 3.0;
  double elevation_ = 0.0;
  bool has_start_ = false;
  Vec3 start_{};
  Vec3 end_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
