#pragma once

#include "bim/grid.h"
#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/document/mesh_asset.h"
#include "entity/family/host/structural/column_entity.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {

// 轴网布柱：在给定轴线的**两两交点**（编号轴 × 字母轴）上各放一根柱，一次布置
// = 一条命令 = 一步撤销（和翻模一样，undo 栈不存快照，所以实体 + 网格自己留一份）。
//
// - `axis_ids` 为空 = 用整张轴网；给了 id = 只在表里这些轴之间求交（框选轴网的落点）。
// - 柱摆在哪一层是**执行那一刻**的当前楼层（和单根柱 `create_column` 一个规矩）：
//   轴网是平面参考（y 恒为 0），柱底标高由楼层标高定。
// - 同一层里已经站在同一个平面点的柱不再重复布置：重复框一次不会叠出两根柱子。
//   `created_count` / `skipped_count` 把这件事讲清楚，状态栏据此回显。
class CreateColumnsOnGridCommand final : public Command {
 public:
  CreateColumnsOnGridCommand(Document& document, ColumnShape shape, double size_a,
                             double size_b, double height,
                             std::vector<std::uint64_t> axis_ids = {});

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  // 真正布置出来的柱数（跳过已经在位的不算）。
  [[nodiscard]] std::size_t created_count() const { return created_.size(); }
  // 交点上已经有柱、这次没再放的个数。
  [[nodiscard]] std::size_t skipped_count() const { return skipped_; }

 private:
  struct Created {
    std::unique_ptr<Entity> entity;
    MeshAsset mesh{};
  };

  Result<void> build();
  void drop_all();

  Document* document_ = nullptr;
  ColumnShape shape_ = ColumnShape::Rectangular;
  double size_a_ = 0.4;  // 矩形 = width，圆形 = diameter
  double size_b_ = 0.4;  // 矩形 = depth，圆形未用
  double height_ = 3.0;
  std::vector<std::uint64_t> axis_ids_;
  std::vector<Created> created_;
  std::size_t skipped_ = 0;
  bool built_ = false;
};

}  // namespace tamias
