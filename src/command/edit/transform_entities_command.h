#pragma once

#include "bim/host_placement.h"
#include "command/core/command.h"
#include "command/edit/entity_transform.h"
#include "engine/document/document.h"

#include <cstdint>
#include <vector>

namespace tamias {

// 变换一批选中实体（平移 / 绕竖直轴旋转 / 任意刚体摆放），一条撤销。
//
// 与「创建」类命令不同，这里改的是已有实体的世界摆放：
//   - 族实体反写 Location（墙的长度、朝向跟着走）；
//   - 草图 / 基础体直接写 local_transform；
//   - 门窗的摆放由宿主墙决定，改的是 HostedOn 关联里的沿墙参数。
// 变换做完后重算墙-墙交接与宿主开洞（邻墙、洞口的网格都要跟着变）。
//
// 注意：必须是刚体变换（平移 + 绕 Y 旋转）。镜像走 MirrorEntitiesCommand——
// 反射矩阵会破坏拾取与空间索引的刚体假设（见 entity_transform.h）。
class TransformEntitiesCommand final : public Command {
 public:
  TransformEntitiesCommand(Document& document, std::vector<EntityTransform> items);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  // 宿主开口的撤销快照：摆放参数改了要能还回去。
  struct GuestBinding {
    std::uint64_t relation_id = 0;  // 0 = 这一项不是宿主开口
    HostPlacement placement{};
    bool valid = true;
  };

  Result<void> apply(bool to_target);
  void capture_guests();

  Document* document_ = nullptr;
  std::vector<EntityTransform> items_;
  std::vector<GuestBinding> guests_;  // 与 items_ 等长
  bool captured_ = false;
};

}  // namespace tamias
