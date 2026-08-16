#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "engine/modeling/feature.h"

#include <cstdint>
#include <memory>

namespace tamias {

// 布尔运算：把实体 B 的特征树合并进实体 A，追加一个 Boolean 特征，重算 A 并删除 B（可撤销）。
// 结果实体仍是 A（保留 A 的身份），B 在 execute 时被移除、undo 时恢复。
//
// 注意：合并的是两个实体的「局部几何」（各自特征树局部坐标系），结果继承 A 的放置。两个实体
// 相对位置（local_transform 差值）的烘焙需要 Transform 特征，留待后续——当前适用于两实体
// 同放置的场景。
class BooleanCommand final : public Command {
 public:
  BooleanCommand(Document& document, std::uint64_t a_id, std::uint64_t b_id, BooleanOp op);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Result<void> apply(bool combined);

  Document* document_ = nullptr;
  std::uint64_t a_id_ = 0;
  std::uint64_t b_id_ = 0;
  BooleanOp op_ = BooleanOp::Fuse;

  FeatureModel a_model_old_;
  MeshAsset a_mesh_old_{};
  std::unique_ptr<Entity> b_entity_;
  MeshAsset b_mesh_{};
};

}  // namespace tamias
