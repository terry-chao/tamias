#pragma once

#include "bim/relation.h"
#include "command/command.h"
#include "engine/document/document.h"
#include "entity/structural/column_entity.h"

#include <optional>

namespace tamias {

// 单点放置的参数化构件。
enum class PrimitiveKind { Column, Door, Window };

// 创建一个参数化基础几何体（交互式）：dispatch 后武装，点一个位置确定，然后 createGeom 造型。
class CreatePrimitiveCommand final : public Command {
 public:
  CreatePrimitiveCommand(Document& document, PrimitiveKind kind);
  CreatePrimitiveCommand(Document& document, PrimitiveKind kind, Vec3 position,
                         std::uint64_t host_id = 0);
  // 门窗脚本式：预设位置 + 宿主 + 开洞尺寸（含离地高度）。
  CreatePrimitiveCommand(Document& document, PrimitiveKind kind, Vec3 position,
                         std::uint64_t host_id, double width, double height,
                         double thickness, double sill);

  // 柱子专用：带子类型与截面/高度参数（交互式，无预设位置）。
  CreatePrimitiveCommand(Document& document, ColumnShape shape, double size_a,
                         double size_b, double height);

  // 柱子专用：带子类型与截面/高度参数 + 预设位置（脚本式）。
  CreatePrimitiveCommand(Document& document, PrimitiveKind kind, Vec3 position,
                         ColumnShape shape, double size_a, double size_b, double height);

  // 门/窗专用：带宽×高×厚参数（交互式，无预设位置）。
  CreatePrimitiveCommand(Document& document, PrimitiveKind kind, double width,
                         double height, double thickness);
  // 门/窗专用：带宽×高×厚×离地高度参数（交互式，无预设位置）。
  CreatePrimitiveCommand(Document& document, PrimitiveKind kind, double width,
                         double height, double thickness, double sill);

  [[nodiscard]] bool interactive() const override { return !scripted_; }
  [[nodiscard]] Result<bool> on_point(Vec3 point) override;
  [[nodiscard]] Result<bool> on_pick(Vec3 point, std::uint64_t picked_entity_id) override;
  void on_hover(Vec3 point, std::uint64_t picked_entity_id) override;
  [[nodiscard]] std::vector<Vec3> preview_polyline(Vec3 cursor) const override;
  [[nodiscard]] float work_plane_y() const override { return work_plane_y_; }

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t mesh_id() const { return mesh_.id; }

 private:
  Document* document_ = nullptr;
  PrimitiveKind kind_ = PrimitiveKind::Column;
  float work_plane_y_ = 0.f;
  bool scripted_ = false;
  Vec3 position_{};
  std::uint64_t host_id_ = 0;
  Vec3 hover_point_{};
  std::uint64_t hover_host_id_ = 0;
  // 柱子参数（仅 kind_ == Column 时使用）。
  ColumnShape column_shape_ = ColumnShape::Rectangular;
  double column_size_a_ = 0.4;  // 矩形=width，圆形=diameter
  double column_size_b_ = 0.4;  // 矩形=depth，圆形=未用
  double column_height_ = 3.0;
  // 门/窗参数（仅 kind_ == Door/Window 时使用）。
  double opening_width_ = 1.0;
  double opening_height_ = 2.1;
  double opening_thickness_ = 0.05;
  double opening_sill_ = 0.9;
  MeshAsset mesh_{};
  std::unique_ptr<Entity> entity_;
  std::optional<Relation> relation_;
};

}  // namespace tamias
