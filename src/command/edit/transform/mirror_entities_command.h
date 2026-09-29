#pragma once

#include "bim/host_placement.h"
#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/math/math.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace tamias {

// 把一批实体镜像到「过 axis_a、沿 axis_b - axis_a」的竖直平面，一条撤销。
//
// 为什么单独一个命令：镜像是**反射**，写进 local_transform 会破坏拾取与空间索引
// 的刚体假设。所以这里把镜像烘焙进实体自身数据（Location 锚点 / 草图控制点），
// 撤销靠 before 快照整体还原。
class MirrorEntitiesCommand final : public Command {
 public:
  MirrorEntitiesCommand(Document& document, std::vector<std::uint64_t> entity_ids, Vec3 axis_a,
                        Vec3 axis_b);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  struct Item {
    std::uint64_t id = 0;
    std::unique_ptr<Entity> before;  // 镜像前的完整快照（模型 + Location + 摆放 + 夹点）
    std::uint64_t relation_id = 0;   // 门窗：摆放由宿主关联决定，撤销要还回原参数
    HostPlacement placement{};
    bool valid = true;
  };

  void capture();
  Result<void> apply(bool to_target);

  Document* document_ = nullptr;
  std::vector<std::uint64_t> entity_ids_;
  Vec3 axis_a_{};
  Vec3 axis_b_{};
  std::vector<Item> items_;
  bool captured_ = false;
};

}  // namespace tamias
