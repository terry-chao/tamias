#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "bim/wall_size.h"

namespace tamias {

// 创建参数化板（交互式）：两个对角点确定水平矩形，厚度由参数给定。
class CreateSlabCommand final : public Command {
 public:
  CreateSlabCommand(Document& document, double thickness, double elevation);

  [[nodiscard]] bool interactive() const override { return true; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] bool has_start() const override { return has_start_; }
  [[nodiscard]] Vec3 start() const override { return start_; }
  [[nodiscard]] float work_plane_y() const override {
    return static_cast<float>(elevation_);
  }
  [[nodiscard]] std::vector<Vec3> preview_polyline(Vec3 cursor) const override;

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  double thickness_ = 0.2;
  double elevation_ = kDefaultWallHeight;
  bool has_start_ = false;
  Vec3 start_{};
  Vec3 end_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
