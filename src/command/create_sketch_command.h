#pragma once

#include "command/command.h"
#include "engine/document/document.h"

#include <vector>

namespace tamias {

enum class SketchKind { Line, Polyline, Circle, Arc, Bezier, Rectangle, BSpline };

// 创建草图曲线（交互式）：按类型收集 2 / 3 / 4 / N 个点后 createGeom。
class CreateSketchCommand final : public Command {
 public:
  CreateSketchCommand(Document& document, SketchKind kind);

  [[nodiscard]] bool interactive() const override { return true; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] bool has_start() const override { return !points_.empty(); }
  [[nodiscard]] Vec3 start() const override { return points_.empty() ? Vec3{} : points_.front(); }
  [[nodiscard]] std::vector<Vec3> preview_polyline(Vec3 cursor) const override;
  [[nodiscard]] std::vector<Vec3> preview_control_polyline(Vec3 cursor) const override;
  [[nodiscard]] std::vector<Vec3> preview_points(Vec3 cursor) const override;
  [[nodiscard]] bool accepts_confirm() const override { return open_ended(); }
  [[nodiscard]] Result<bool> on_confirm() override;

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  [[nodiscard]] bool open_ended() const;
  [[nodiscard]] bool shows_control_polygon() const;
  [[nodiscard]] int required_points() const;
  [[nodiscard]] std::vector<Vec3> live_controls(Vec3 cursor) const;
  [[nodiscard]] Result<std::unique_ptr<Entity>> make_sketch() const;

  Document* document_ = nullptr;
  SketchKind kind_ = SketchKind::Line;
  std::vector<Vec3> points_;
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
};

}  // namespace tamias
