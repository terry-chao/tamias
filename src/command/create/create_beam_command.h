#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "entity/family/host/structural/beam_entity.h"

#include <string_view>

namespace tamias {

// 创建参数化梁（交互式）：两个点确定跨度和方向，截面由子类型与参数给定。
//
// 交互方式：execute() 自己起一个 BeamDrag 采两个屏幕点（阻塞到点齐 / 取消）。
// 因此它**不是** CommandSystem 的 pending 命令——点由 drag 采，不走 feed_point。
class CreateBeamCommand final : public Command {
 public:
  // 矩形梁。
  // elevation：相对**本层标高**的偏移。默认给本层层高 = 落在本层顶（梁在楼面下），
  // 和 create_slab 的"本层顶板"一个约定。
  CreateBeamCommand(Document& document, double width, double depth, double elevation);
  // T 形 / 工字梁。
  CreateBeamCommand(Document& document, BeamShape shape, double flange_width,
                     double web_thickness, double height, double flange_thickness,
                     double elevation);
  // 脚本式（带两端点）。
  CreateBeamCommand(Document& document, double width, double depth, Vec3 start, Vec3 end,
                     double elevation);

  // 脚本式（点已给齐）直接执行；交互式由 execute() 内部的 drag 采点。
  [[nodiscard]] bool interactive() const override { return false; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] bool has_start() const override { return has_start_; }
  [[nodiscard]] Vec3 start() const override { return start_; }

  [[nodiscard]] CommandArgs echo_args() const override;
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
  // 已算上本层标高的绝对标高（和 CreateSlabCommand::elevation_ 同义）。
  double elevation_ = 0.0;

  bool has_start_ = false;
  bool scripted_ = false;
  Vec3 start_{};
  Vec3 end_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
