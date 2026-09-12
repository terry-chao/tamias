#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "entity/structural/foundation_entity.h"

namespace tamias {

// 创建参数化基础（交互式）：单点放置。独立/条形/筏板为矩形，桩为圆柱。
class CreateFoundationCommand final : public Command {
 public:
  // 矩形基础（独立/条形/筏板）。
  CreateFoundationCommand(Document& document, double length, double width, double height);
  // 桩基础。
  CreateFoundationCommand(Document& document, double diameter, double height);
  // 脚本式（带位置，矩形）。
  CreateFoundationCommand(Document& document, double length, double width, double height,
                           Vec3 position);

  [[nodiscard]] bool interactive() const override { return !scripted_; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] float work_plane_y() const override { return work_plane_y_; }

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  FoundationShape shape_ = FoundationShape::Isolated;
  double length_ = 1.5;
  double width_ = 1.5;
  double height_ = 0.5;
  double diameter_ = 0.6;  // 桩
  float work_plane_y_ = 0.f;
  bool scripted_ = false;
  Vec3 position_{};
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
