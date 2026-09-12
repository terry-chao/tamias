#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "entity/structural/beam_entity.h"

namespace tamias {

// 创建参数化梁（交互式）：两个点确定跨度和方向，截面由子类型与参数给定。
class CreateBeamCommand final : public Command {
 public:
  // 矩形梁。
  CreateBeamCommand(Document& document, double width, double depth);
  // T 形 / 工字梁。
  CreateBeamCommand(Document& document, BeamShape shape, double flange_width,
                     double web_thickness, double height, double flange_thickness);
  // 脚本式（带两端点）。
  CreateBeamCommand(Document& document, double width, double depth, Vec3 start, Vec3 end);

  [[nodiscard]] bool interactive() const override { return !scripted_; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] bool has_start() const override { return has_start_; }
  [[nodiscard]] Vec3 start() const override { return start_; }

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  BeamShape shape_ = BeamShape::Rectangular;
  // 矩形梁。
  double width_ = 0.3;
  double depth_ = 0.5;
  // T 形 / 工字梁。
  double flange_width_ = 0.4;
  double web_thickness_ = 0.2;
  double height_ = 0.5;
  double flange_thickness_ = 0.1;

  bool has_start_ = false;
  bool scripted_ = false;
  Vec3 start_{};
  Vec3 end_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
