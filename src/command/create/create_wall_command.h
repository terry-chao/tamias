#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"

namespace tamias {

// 创建一面参数化墙（交互式）：dispatch 后武装，拖拽两个点确定墙，然后 createGeom 造型。
class CreateWallCommand final : public Command {
 public:
  // 实心墙（默认）。
  CreateWallCommand(Document& document, double thickness, double height);
  // 空心墙：leaf 单侧壁厚。
  CreateWallCommand(Document& document, double thickness, double height, double leaf);
  // 脚本式（实心）。
  CreateWallCommand(Document& document, double thickness, double height, Vec3 start, Vec3 end);

  [[nodiscard]] bool interactive() const override { return !scripted_; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] bool has_start() const override { return has_start_; }
  [[nodiscard]] Vec3 start() const override { return start_; }
  [[nodiscard]] float work_plane_y() const override {
    return static_cast<float>(elevation_);
  }

  [[nodiscard]] CommandArgs echo_args() const override;
  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  double thickness_ = 0.2;
  double height_ = 3.0;
  double leaf_ = 0.0;  // >0 表示空心墙
  double elevation_ = 0.0;
  // 武装这一刻的当前楼层：标高与楼层归属都照它算（点齐前用户可能切了楼层，
  // 归属不能跟着漂，见 docs/BIM.md）。
  std::uint64_t placement_storey_ = 0;
  bool has_start_ = false;
  bool scripted_ = false;
  Vec3 start_{};
  Vec3 end_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
